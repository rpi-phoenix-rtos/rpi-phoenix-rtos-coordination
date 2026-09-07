/*
 * fileperf -- per-syscall cost of touching many small files.
 *
 * Measurements through `cat`/`ls` in a shell loop time fork+exec, and even
 * `tar` bundles open/read/close/stat with its own per-file bookkeeping.  This
 * removes all of that: one process, one loop, each syscall timed separately, so
 * the ~100 ms/file cost seen on BOTH nfs and a ramdisk can be attributed to a
 * specific call rather than to a harness.
 *
 * Usage: fileperf <dir> [max_files]
 *        fileperf --stat <path>...      time stat() on each path
 *        fileperf --mkdepth <root>      cost of one more REAL path component
 *
 * Copyright 2026 Phoenix Systems  %LICENSE%
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>

static long long now_us(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);

	return ((long long)ts.tv_sec * 1000000LL) + (ts.tv_nsec / 1000);
}

/* Depth mode: stat the SAME file through paths that differ only in how many
 * components they have, by padding with "./" segments.
 *
 * INVALID -- kept only so the mistake is not repeated.  "." segments are
 * normalised away before any path is sent to a server, so this cannot detect
 * depth by construction, and its flat result is not evidence of anything.  Use
 * --mkdepth, which builds REAL nested directories. */
static int depth_mode(const char *path)
{
	int extra;

	printf("FILEPERF depth-mode target=%s\n", path);
	for (extra = 0; extra <= 8; extra += 2) {
		char p[1024];
		long long t0;
		int i, k, ok = 0;
		size_t n;

		/* build "<dir>/./././<base>" with `extra` dot segments */
		p[0] = '\0';
		{
			const char *slash = strrchr(path, '/');
			size_t dlen = (slash != NULL) ? (size_t)(slash - path) : 0;

			if (dlen > 0) {
				memcpy(p, path, dlen);
				p[dlen] = '\0';
			}
			for (k = 0; k < extra; k++) {
				strncat(p, "/.", sizeof(p) - strlen(p) - 1);
			}
			strncat(p, (slash != NULL) ? slash : "/", sizeof(p) - strlen(p) - 1);
		}
		n = strlen(p);

		t0 = now_us();
		for (i = 0; i < 20; i++) {
			struct stat st;

			if (stat(p, &st) == 0) {
				ok++;
			}
		}
		printf("FILEPERF depth extra=%d comps~%d len=%zu ok=%d stat_avg=%lldus\n",
				extra, extra + 2, n, ok, (now_us() - t0) / 20);
	}

	return 0;
}


/* Real-depth mode: build <root>/d1/d2/... with a file "x" at every level, then
 * stat each of those x's.  Component count is the ONLY thing that varies, and
 * the directories genuinely exist, so this measures what resolving one more
 * component actually costs.
 *
 * Reports the FIRST call separately from the average of the rest: with an
 * attribute cache in the filesystem server the two differ, and conflating them
 * is how a cache gets credited with more than it does.  Point it at a ramdisk
 * to see the cost that is NOT network round trips.
 *
 * Leaves the tree behind on purpose (cheap to inspect, trivially rm -r'd). */
static int mkdepth_mode(const char *root)
{
	char dir[512];
	char leaf[600];
	int d;

	printf("FILEPERF mkdepth root=%s\n", root);
	if ((mkdir(root, 0777) != 0) && (errno != EEXIST)) {
		printf("FILEPERF mkdepth FAILED mkdir %s errno=%d\n", root, errno);
		return 1;
	}
	snprintf(dir, sizeof(dir), "%s", root);

	for (d = 0; d <= 8; d++) {
		long long t0, tfirst;
		struct stat st;
		int i, ok = 0, fd;

		if (d > 0) {
			/* Append in place: one buffer, so there is no size to get wrong. */
			size_t dlen = strlen(dir);

			snprintf(dir + dlen, sizeof(dir) - dlen, "/d%d", d);
			if ((mkdir(dir, 0777) != 0) && (errno != EEXIST)) {
				printf("FILEPERF mkdepth FAILED mkdir %s errno=%d\n", dir, errno);
				return 1;
			}
		}

		snprintf(leaf, sizeof(leaf), "%s/x", dir);
		fd = open(leaf, O_WRONLY | O_CREAT | O_TRUNC, 0666);
		if (fd < 0) {
			printf("FILEPERF mkdepth FAILED create %s errno=%d\n", leaf, errno);
			return 1;
		}
		(void)write(fd, "x", 1);
		close(fd);

		/* First stat of this path in this process: nothing above it is warm
		 * except what the previous depth touched (its parent chain). */
		t0 = now_us();
		if (stat(leaf, &st) == 0) {
			ok++;
		}
		tfirst = now_us() - t0;

		t0 = now_us();
		for (i = 0; i < 20; i++) {
			if (stat(leaf, &st) == 0) {
				ok++;
			}
		}
		printf("FILEPERF mkdepth comps=%2d ok=%2d first=%6lldus warm_avg=%6lldus  %s\n",
				d + 1, ok, tfirst, (now_us() - t0) / 20, leaf);
	}

	return 0;
}


int main(int argc, char **argv)
{
	const char *dir = (argc > 1) ? argv[1] : ".";

	if ((argc > 2) && (strcmp(argv[1], "--depth") == 0)) {
		return depth_mode(argv[2]);
	}

	if ((argc > 2) && (strcmp(argv[1], "--mkdepth") == 0)) {
		return mkdepth_mode(argv[2]);
	}

	/* --stat: time stat() on each given path.  Comparing a MOUNT POINT against a
	 * file inside it, and a 1-component file on the (nfs) root against a deep
	 * one, says whether the cost is paid crossing the root mount or inside each
	 * server. */
	if ((argc > 2) && (strcmp(argv[1], "--stat") == 0)) {
		int a;

		for (a = 2; a < argc; a++) {
			long long t0 = now_us();
			int i, ok = 0;

			for (i = 0; i < 20; i++) {
				struct stat st;

				if (stat(argv[a], &st) == 0) {
					ok++;
				}
			}
			printf("FILEPERF stat %-42s ok=%2d avg=%lldus\n", argv[a], ok,
					(now_us() - t0) / 20);
		}

		return 0;
	}
	int maxf = (argc > 2) ? atoi(argv[2]) : 200;
	long long t_open = 0, t_read = 0, t_close = 0, t_stat = 0, t_readdir = 0;
	long long bytes = 0, t0;
	int n = 0, nstat = 0;
	static char buf[65536];
	DIR *d;

	t0 = now_us();
	d = opendir(dir);
	if (d == NULL) {
		fprintf(stderr, "fileperf: cannot open dir %s\n", dir);
		return 1;
	}

	for (;;) {
		struct dirent *de;
		char path[512];
		long long a, b;
		int fd;

		a = now_us();
		de = readdir(d);
		t_readdir += now_us() - a;
		if (de == NULL) {
			break;
		}
		if ((strcmp(de->d_name, ".") == 0) || (strcmp(de->d_name, "..") == 0)) {
			continue;
		}
		if (n >= maxf) {
			break;
		}
		snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);

		{
			struct stat st;

			a = now_us();
			if (stat(path, &st) == 0) {
				t_stat += now_us() - a;
				nstat++;
				if (!S_ISREG(st.st_mode)) {
					continue;
				}
			}
			else {
				t_stat += now_us() - a;
				continue;
			}
		}

		a = now_us();
		fd = open(path, O_RDONLY);
		b = now_us();
		t_open += b - a;
		if (fd < 0) {
			continue;
		}

		for (;;) {
			ssize_t r;

			a = now_us();
			r = read(fd, buf, sizeof(buf));
			t_read += now_us() - a;
			if (r <= 0) {
				break;
			}
			bytes += r;
		}

		a = now_us();
		close(fd);
		t_close += now_us() - a;
		n++;
	}
	closedir(d);

	printf("FILEPERF dir=%s files=%d bytes=%lld total=%lldms\n", dir, n, bytes,
			(now_us() - t0) / 1000);
	printf("FILEPERF per-file us: readdir=%lld stat=%lld open=%lld read=%lld close=%lld\n",
			(n > 0) ? (t_readdir / n) : 0, (nstat > 0) ? (t_stat / nstat) : 0,
			(n > 0) ? (t_open / n) : 0, (n > 0) ? (t_read / n) : 0,
			(n > 0) ? (t_close / n) : 0);

	return 0;
}
