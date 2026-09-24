/*
 * writecost.c - what does one write() actually cost, and does size matter?
 *
 * P6 says `echo > file` is ~50x slower than dd "on real storage" and is
 * "invisible on a RAM filesystem". A re-measurement on hardware shows the same
 * ~8x on /ramtmp as on NFS, which points away from storage entirely: 513 bytes
 * of payload taking ~0.8 s looks like one write() per byte at ~1.5 ms each.
 *
 * This isolates that claim from bash: same total bytes, different write sizes,
 * on whatever directory is given. If the per-call cost dominates, 512 x 1-byte
 * will cost ~512x one 512-byte write, and the filesystem will barely matter.
 *
 * Copyright 2026 Phoenix Systems  %LICENSE%
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

static long long now_us(void)
{
	struct timespec ts;

	(void)clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000LL;
}


/* Write `total` bytes to a fresh file in `dir`, `chunk` bytes per write(). */
static void run(const char *dir, const char *tag, size_t total, size_t chunk)
{
	char path[256];
	char buf[4096];
	long long t0, t1;
	size_t done = 0;
	unsigned long calls = 0;
	int fd;

	(void)snprintf(path, sizeof(path), "%s/wc_%s", dir, tag);
	(void)memset(buf, 'x', sizeof(buf));

	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) {
		printf("WCRESULT %s %s SKIP open-failed\n", dir, tag);
		fflush(stdout);
		return;
	}

	t0 = now_us();
	while (done < total) {
		size_t n = ((total - done) < chunk) ? (total - done) : chunk;
		ssize_t w = write(fd, buf, n);

		if (w <= 0) {
			break;
		}
		done += (size_t)w;
		calls++;
	}
	t1 = now_us();
	(void)close(fd);
	(void)unlink(path);

	/* Report the per-call cost, which is the number under test -- and the byte
	 * count, so a short write cannot masquerade as a fast one. */
	printf("WCRESULT %s %-10s bytes=%zu chunk=%zu calls=%lu total_us=%lld per_call_us=%lld\n",
		dir, tag, done, chunk, calls, t1 - t0,
		(calls != 0UL) ? ((t1 - t0) / (long long)calls) : -1LL);
	fflush(stdout);
}



/* Where does the ~1.5 ms actually go? A file write() on a microkernel is an IPC
 * round trip to a filesystem server, so compare against calls that do less:
 *   getpid  - a syscall that never leaves the kernel
 *   devnull - IPC to devfs, but no filesystem work
 *   file    - IPC to the fs server, with filesystem work
 * If getpid is microseconds and the other two are milliseconds, the cost is the
 * round trip, not the syscall entry and not the storage.
 */
static void run_syscall_floor(const char *dir)
{
	char path[256];
	long long t0, t1;
	int i, fd;
	const int N = 200;

	t0 = now_us();
	for (i = 0; i < N; i++) {
		(void)getpid();
	}
	t1 = now_us();
	printf("WCRESULT floor getpid     calls=%d total_us=%lld per_call_us=%lld\n",
		N, t1 - t0, (t1 - t0) / N);
	fflush(stdout);

	fd = open("/dev/null", O_WRONLY);
	if (fd >= 0) {
		t0 = now_us();
		for (i = 0; i < N; i++) {
			(void)write(fd, "x", 1);
		}
		t1 = now_us();
		(void)close(fd);
		printf("WCRESULT floor devnull-w  calls=%d total_us=%lld per_call_us=%lld\n",
			N, t1 - t0, (t1 - t0) / N);
	}
	else {
		printf("WCRESULT floor devnull-w  SKIP open-failed\n");
	}
	fflush(stdout);

	(void)snprintf(path, sizeof(path), "%s/wc_floor", dir);
	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd >= 0) {
		t0 = now_us();
		for (i = 0; i < N; i++) {
			(void)write(fd, "x", 1);
		}
		t1 = now_us();
		(void)close(fd);
		(void)unlink(path);
		printf("WCRESULT floor file-w     calls=%d total_us=%lld per_call_us=%lld\n",
			N, t1 - t0, (t1 - t0) / N);
	}
	else {
		printf("WCRESULT floor file-w     SKIP open-failed\n");
	}
	fflush(stdout);
}


int main(int argc, char **argv)
{
	const char *dir = (argc > 1) ? argv[1] : "/ramtmp";
	size_t total = 4096;

	printf("WCBENCH start dir=%s total=%zu\n", dir, total);
	fflush(stdout);

	run(dir, "x1", total, 1);      /* one byte per write  */
	run(dir, "x16", total, 16);
	run(dir, "x512", total, 512);
	run(dir, "x4096", total, 4096); /* one write           */

	run_syscall_floor(dir);

	printf("WCBENCH done\n");
	fflush(stdout);
	return 0;
}
