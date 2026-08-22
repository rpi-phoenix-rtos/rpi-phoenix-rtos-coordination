/* SPDX-License-Identifier: Zlib
 *
 * ram-stage-play.c — stage a game's asset tree from NFS into a tmpfs (RAM), then
 * exec the game so it loads from RAM. Productizes the RAM-staging load-time
 * workaround for the Phoenix-RTOS Raspberry Pi 4 netboot: game assets read over
 * NFS are latency-bound (~1.46 ms per scattered 4 KiB read) vs ~0.07 ms from the
 * /tmp dummyfs (RAM) — measured ~20x per read, and quakespasm Q1 loads ~3.6x
 * faster from RAM end-to-end; Quake2 (47 MiB) renders the full 3D level from a
 * RAM-staged copy. The /tmp RAM-disk is enlarged to 256 MiB on rpi4b
 * (board_config.h DUMMYFS_SIZE_MAX) so a big game's assets fit.
 *
 *   ram-stage-play <src-dir> <dst-dir> <exec> [exec-args...]
 *
 * Recursively copies <src-dir> (typically an NFS path such as
 * /usr/share/quake2/baseq2) to <dst-dir> (a /tmp tmpfs path), then execv()s
 * <exec> with the remaining args (which should point the game's basedir at the
 * RAM copy). One command, so a shell without `&&`/`;` (psh) can stage-and-play.
 *
 *   ram-stage-play /usr/share/quake2/baseq2 /tmp/baseq2 \
 *       /usr/bin/yquake2 +set basedir /tmp +set vid_renderer gl1 +map demo1
 *
 * Standalone static aarch64-phoenix ELF; links libphoenix only.
 *
 * Copyright 2026 Phoenix Systems
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>

static unsigned long long g_bytes = 0;
static unsigned long g_files = 0;
static unsigned long long g_next_report = 16ull * 1024 * 1024; /* progress every 16 MiB */

static int copy_file(const char *src, const char *dst, mode_t mode)
{
	int sfd = open(src, O_RDONLY);
	if (sfd < 0) {
		printf("ram-stage: open src '%s': %s\n", src, strerror(errno));
		return -1;
	}
	int dfd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, mode);
	if (dfd < 0) {
		printf("ram-stage: open dst '%s': %s\n", dst, strerror(errno));
		close(sfd);
		return -1;
	}
	size_t bufsz = 256u * 1024u;
	char *buf = malloc(bufsz);
	if (buf == NULL) {
		printf("ram-stage: OOM\n");
		close(sfd);
		close(dfd);
		return -1;
	}
	int rc = 0;
	for (;;) {
		ssize_t off = 0;
		ssize_t n = read(sfd, buf, bufsz);
		if (n < 0) {
			printf("ram-stage: read '%s': %s\n", src, strerror(errno));
			rc = -1;
			break;
		}
		if (n == 0) {
			break;
		}
		while (off < n) {
			ssize_t w = write(dfd, buf + off, (size_t)(n - off));
			if (w < 0) {
				printf("ram-stage: write '%s': %s\n", dst, strerror(errno));
				rc = -1;
				break;
			}
			off += w;
		}
		if (rc != 0) {
			break;
		}
		g_bytes += (unsigned long long)n;
		/* Periodic progress so the user sees the preload is advancing (the copy
		 * runs before any game frame is drawn, so the screen is otherwise blank). */
		if (g_bytes >= g_next_report) {
			printf("ram-stage: preloading... %llu MiB copied\n", g_bytes / (1024ull * 1024ull));
			g_next_report += 16ull * 1024 * 1024;
		}
	}
	free(buf);
	close(sfd);
	close(dfd);
	if (rc == 0) {
		g_files++;
	}
	return rc;
}

/* Create `path` and all missing parent directories (like `mkdir -p`). Needed because
 * copy_tree mkdir's only the dst itself, but a dst like /tmp/quake/id1 needs its parent
 * /tmp/quake created first. */
static int mkdir_p(const char *path)
{
	char tmp[1024];
	size_t len = strlen(path);
	size_t i;
	if (len == 0u || len >= sizeof(tmp)) {
		return -1;
	}
	memcpy(tmp, path, len + 1u);
	for (i = 1u; i < len; i++) {
		if (tmp[i] == '/') {
			tmp[i] = '\0';
			if (mkdir(tmp, 0755) < 0 && errno != EEXIST) {
				printf("ram-stage: mkdir '%s': %s\n", tmp, strerror(errno));
				return -1;
			}
			tmp[i] = '/';
		}
	}
	if (mkdir(tmp, 0755) < 0 && errno != EEXIST) {
		printf("ram-stage: mkdir '%s': %s\n", tmp, strerror(errno));
		return -1;
	}
	return 0;
}

static int copy_tree(const char *src, const char *dst)
{
	struct stat st;
	if (stat(src, &st) < 0) {
		printf("ram-stage: stat '%s': %s\n", src, strerror(errno));
		return -1;
	}
	if (S_ISDIR(st.st_mode)) {
		DIR *d;
		struct dirent *e;
		int rc = 0;
		if (mkdir(dst, 0755) < 0 && errno != EEXIST) {
			printf("ram-stage: mkdir '%s': %s\n", dst, strerror(errno));
			return -1;
		}
		d = opendir(src);
		if (d == NULL) {
			printf("ram-stage: opendir '%s': %s\n", src, strerror(errno));
			return -1;
		}
		while ((e = readdir(d)) != NULL) {
			char sp[1024], dp[1024];
			if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
				continue;
			}
			snprintf(sp, sizeof(sp), "%s/%s", src, e->d_name);
			snprintf(dp, sizeof(dp), "%s/%s", dst, e->d_name);
			if (copy_tree(sp, dp) != 0) {
				rc = -1;
				break;
			}
		}
		closedir(d);
		return rc;
	}
	return copy_file(src, dst, 0644);
}

int main(int argc, char **argv)
{
	const char *src, *dst, *exe;
	char **exeargv;
	int ai = 1, exec_ram = 0;
	struct timespec t0, t1;
	double secs, mib;

	setvbuf(stdout, NULL, _IONBF, 0);
	/* Optional leading --exec-ram: also stage the game BINARY into RAM (/tmp, 0755) and
	 * exec it from there, so the binary's demand-paging is RAM-speed too (not NFS). */
	if (argc > 1 && strcmp(argv[1], "--exec-ram") == 0) {
		exec_ram = 1;
		ai = 2;
	}
	if (argc < ai + 3) {
		printf("usage: ram-stage-play [--exec-ram] <src-dir> <dst-dir> <exec> [exec-args...]\n");
		return 1;
	}
	src = argv[ai];
	dst = argv[ai + 1];
	exe = argv[ai + 2];
	exeargv = &argv[ai + 2];   /* {exe, args..., NULL} */

	printf("ram-stage: ===== starting to preload game data to RAM disk =====\n");
	printf("ram-stage: copying %s -> %s (please wait — the screen stays blank until this finishes)\n", src, dst);
	clock_gettime(CLOCK_MONOTONIC, &t0);
	if (mkdir_p(dst) != 0) {
		printf("ram-stage: could not create dst path '%s'\n", dst);
		return 2;
	}
	if (copy_tree(src, dst) != 0) {
		printf("ram-stage: staging FAILED after %llu B / %lu files (tmpfs full? see errno above)\n",
		       g_bytes, g_files);
		return 2;
	}
	clock_gettime(CLOCK_MONOTONIC, &t1);
	secs = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;
	mib = (double)g_bytes / (1024.0 * 1024.0);
	printf("ram-stage: ===== DONE creating RAM disk: %lu files, %.2f MiB in %.3f s (%.2f MiB/s) — launching game =====\n",
	       g_files, mib, secs, (secs > 0.0) ? (mib / secs) : 0.0);

	if (exec_ram) {
		const char *base = strrchr(exe, '/');
		char rampath[512];
		unsigned long long b0 = g_bytes;
		base = (base != NULL) ? base + 1 : exe;
		snprintf(rampath, sizeof(rampath), "/tmp/%s", base);
		if (copy_file(exe, rampath, 0755) != 0) {
			printf("ram-stage: failed to stage exec '%s' -> '%s'\n", exe, rampath);
			return 4;
		}
		printf("ram-stage: staged exec %s -> %s (%.2f MiB); exec from RAM\n",
		       exe, rampath, (double)(g_bytes - b0) / (1024.0 * 1024.0));
		exeargv[0] = rampath;   /* run the RAM copy */
		execv(rampath, exeargv);
		printf("ram-stage: execv '%s' failed: %s\n", rampath, strerror(errno));
		return 3;
	}

	printf("ram-stage: exec %s\n", exe);
	execv(exe, exeargv);
	printf("ram-stage: execv '%s' failed: %s\n", exe, strerror(errno));
	return 3;
}
