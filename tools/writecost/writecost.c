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
#include <sys/stat.h>
#include <stdint.h>
#include <pthread.h>
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



/* Is the ~1.5 ms specific to write(), or does every operation on a regular file
 * pay it? lseek moves no data and touches no storage; fstat returns metadata the
 * server already holds. If those cost the same as write(), the price is being
 * paid per FILE OPERATION, and looking inside the write path would be the wrong
 * place to look. */
static void run_op_mix(const char *dir)
{
	char path[256];
	char buf[64];
	struct stat st;
	long long t0, t1;
	int i, fd;
	const int N = 200;

	(void)snprintf(path, sizeof(path), "%s/wc_ops", dir);
	fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) {
		printf("WCRESULT ops SKIP open-failed\n");
		fflush(stdout);
		return;
	}
	(void)memset(buf, 'x', sizeof(buf));
	(void)write(fd, buf, sizeof(buf));

	t0 = now_us();
	for (i = 0; i < N; i++) {
		(void)lseek(fd, 0, SEEK_SET);
	}
	t1 = now_us();
	printf("WCRESULT ops lseek        calls=%d per_call_us=%lld\n", N, (t1 - t0) / N);

	t0 = now_us();
	for (i = 0; i < N; i++) {
		(void)fstat(fd, &st);
	}
	t1 = now_us();
	printf("WCRESULT ops fstat        calls=%d per_call_us=%lld\n", N, (t1 - t0) / N);

	t0 = now_us();
	for (i = 0; i < N; i++) {
		(void)lseek(fd, 0, SEEK_SET);
		(void)read(fd, buf, sizeof(buf));
	}
	t1 = now_us();
	printf("WCRESULT ops seek+read    calls=%d per_call_us=%lld\n", N, (t1 - t0) / N);

	t0 = now_us();
	for (i = 0; i < N; i++) {
		(void)lseek(fd, 0, SEEK_SET);
		(void)write(fd, buf, sizeof(buf));
	}
	t1 = now_us();
	printf("WCRESULT ops seek+write   calls=%d per_call_us=%lld\n", N, (t1 - t0) / N);

	(void)close(fd);
	(void)unlink(path);
	fflush(stdout);
}



/* msg_map() maps the payload into the receiver, but a buffer that does not start
 * and end on a page boundary takes a COPY path instead (it allocates a page and
 * copies the partial head/tail). So compare a page-aligned, exactly-page-sized
 * write against a deliberately misaligned one of the same length. If the aligned
 * case is much cheaper, the cost is that copy path; if they match, it is the
 * mapping itself and alignment is a red herring. */
static void run_alignment(const char *dir)
{
	char path[256];
	char *raw, *aligned;
	long long t0, t1;
	int i, fd;
	const int N = 100;

	raw = malloc(3 * 4096);
	if (raw == NULL) {
		printf("WCRESULT align SKIP malloc-failed\n");
		fflush(stdout);
		return;
	}
	aligned = (char *)(((uintptr_t)raw + 4095U) & ~(uintptr_t)4095U);
	(void)memset(raw, 'x', 3 * 4096);

	(void)snprintf(path, sizeof(path), "%s/wc_align", dir);
	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) {
		printf("WCRESULT align SKIP open-failed\n");
		free(raw);
		fflush(stdout);
		return;
	}

	t0 = now_us();
	for (i = 0; i < N; i++) {
		(void)lseek(fd, 0, SEEK_SET);
		(void)write(fd, aligned, 4096);
	}
	t1 = now_us();
	printf("WCRESULT align page-aligned-4096  calls=%d per_call_us=%lld\n", N, (t1 - t0) / N);

	t0 = now_us();
	for (i = 0; i < N; i++) {
		(void)lseek(fd, 0, SEEK_SET);
		(void)write(fd, aligned + 17, 4096);
	}
	t1 = now_us();
	printf("WCRESULT align misaligned-4096    calls=%d per_call_us=%lld\n", N, (t1 - t0) / N);

	t0 = now_us();
	for (i = 0; i < N; i++) {
		(void)lseek(fd, 0, SEEK_SET);
		(void)write(fd, aligned, 1);
	}
	t1 = now_us();
	printf("WCRESULT align page-aligned-1B    calls=%d per_call_us=%lld\n", N, (t1 - t0) / N);

	(void)close(fd);
	(void)unlink(path);
	free(raw);
	fflush(stdout);
}



/* Is the ~1.5 ms spent WAITING or WORKING?
 *
 * It is uniform across payload size, alignment and filesystem, and a write to
 * /dev/null through the same transport costs 27 us -- so it is neither the
 * message mapping nor the round trip. A flat cost like that is what a wait for
 * the next timer tick looks like.
 *
 * Distinguish them without any kernel instrumentation: run the same writes from
 * several threads at once. If the time is spent blocked, the waits overlap and
 * N threads finish in about the time of one. If it is CPU work in the server,
 * they serialise and N threads take about N times as long.
 */
static const char *g_dir;
static int g_iters;

static void *writer(void *arg)
{
	char path[256];
	char buf[64];
	int i, fd;

	(void)snprintf(path, sizeof(path), "%s/wc_thr%ld", g_dir, (long)(intptr_t)arg);
	(void)memset(buf, 'x', sizeof(buf));
	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) {
		return NULL;
	}
	for (i = 0; i < g_iters; i++) {
		(void)write(fd, buf, sizeof(buf));
	}
	(void)close(fd);
	(void)unlink(path);
	return NULL;
}


static void run_concurrency(const char *dir)
{
	pthread_t th[4];
	long long t0, t1;
	int nthreads, i;

	g_dir = dir;
	g_iters = 50;

	for (nthreads = 1; nthreads <= 4; nthreads *= 2) {
		t0 = now_us();
		for (i = 0; i < nthreads; i++) {
			if (pthread_create(&th[i], NULL, writer, (void *)(intptr_t)i) != 0) {
				printf("WCRESULT conc SKIP pthread_create-failed\n");
				fflush(stdout);
				return;
			}
		}
		for (i = 0; i < nthreads; i++) {
			(void)pthread_join(th[i], NULL);
		}
		t1 = now_us();
		/* per_write_us falling as threads rise => the time was spent WAITING. */
		printf("WCRESULT conc threads=%d writes=%d wall_us=%lld per_write_us=%lld\n",
			nthreads, nthreads * g_iters, t1 - t0,
			(t1 - t0) / (long long)(nthreads * g_iters));
		fflush(stdout);
	}
}



#define NHOGS 4
static volatile int g_hogStop;

static void *cpu_hog(void *arg)
{
	volatile unsigned long x = 0;

	(void)arg;
	while (g_hogStop == 0) {
		x++;
	}
	return NULL;
}


/* The last fork in the road: is the server BURNING CPU or WAITING?
 *
 * Run the identical write loop with and without a CPU-bound thread alongside.
 * If the 1.5 ms is CPU work, the hog competes for the core and the writes get
 * slower. If the server is asleep waiting for something, the hog simply uses the
 * idle time and the writes are unaffected.
 *
 * Also price pwrite() against lseek()+write(): if a regular-file write costs
 * more than one message -- say an extra round trip to carry the file offset --
 * the two will differ. 1488/56 is about 27 round trips, so this is worth ruling
 * in or out before anyone goes looking inside a server.
 */
static void run_cpu_vs_wait(const char *dir)
{
	char path[256];
	char buf[64];
	pthread_t hogs[NHOGS];
	long long t0, t1, quiet, busy;
	int i, fd;
	const int N = 100;

	(void)snprintf(path, sizeof(path), "%s/wc_hog", dir);
	(void)memset(buf, 'x', sizeof(buf));
	fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) {
		printf("WCRESULT hog SKIP open-failed\n");
		fflush(stdout);
		return;
	}

	t0 = now_us();
	for (i = 0; i < N; i++) {
		(void)lseek(fd, 0, SEEK_SET);
		(void)write(fd, buf, sizeof(buf));
	}
	t1 = now_us();
	quiet = (t1 - t0) / N;
	printf("WCRESULT hog quiet        per_write_us=%lld\n", quiet);
	fflush(stdout);

	/* One hog is useless on a 4-core board -- it just takes an idle core and
	 * never contends with the server. Saturate every core instead. */
	g_hogStop = 0;
	{
		int h, spawned = 0;

		for (h = 0; h < NHOGS; h++) {
			if (pthread_create(&hogs[h], NULL, cpu_hog, NULL) == 0) {
				spawned++;
			}
		}
		printf("WCRESULT hog spawned=%d cores=%ld\n", spawned, sysconf(_SC_NPROCESSORS_ONLN));
		fflush(stdout);
	}
	if (1) {
		t0 = now_us();
		for (i = 0; i < N; i++) {
			(void)lseek(fd, 0, SEEK_SET);
			(void)write(fd, buf, sizeof(buf));
		}
		t1 = now_us();
		busy = (t1 - t0) / N;
		g_hogStop = 1;
		{
			int h;

			for (h = 0; h < NHOGS; h++) {
				(void)pthread_join(hogs[h], NULL);
			}
		}
		printf("WCRESULT hog with-cpu-hog per_write_us=%lld  (>> quiet => CPU work; ~= quiet => waiting)\n",
			busy);
	}
	fflush(stdout);

	/* one message or several? */
	t0 = now_us();
	for (i = 0; i < N; i++) {
		(void)pwrite(fd, buf, sizeof(buf), 0);
	}
	t1 = now_us();
	printf("WCRESULT msgs pwrite      per_call_us=%lld\n", (t1 - t0) / N);

	t0 = now_us();
	for (i = 0; i < N; i++) {
		(void)lseek(fd, 0, SEEK_SET);
		(void)write(fd, buf, sizeof(buf));
	}
	t1 = now_us();
	printf("WCRESULT msgs seek+write  per_call_us=%lld\n", (t1 - t0) / N);

	(void)close(fd);
	(void)unlink(path);
	fflush(stdout);
}



/* Does a no-payload operation reach the server as fast as fstat claims?
 *
 * fstat on a /ramtmp file measures 56 us while write on the same fd measures
 * 1488 us -- same client, same kernel path, same server. Either the round trip
 * really is ~56 us and the cost is specific to carrying data, or fstat never
 * reached dummyfs at all (answered from kernel state) and the round trip itself
 * is the 1.5 ms.
 *
 * ftruncate carries no payload but MUST reach the filesystem (mtTruncate), so it
 * separates the two: ~56 us means the server is fast and data is the problem;
 * ~1.5 ms means every round trip to this server is slow and fstat was a red
 * herring.
 */
static void run_roundtrip(const char *dir)
{
	char path[256];
	long long t0, t1;
	int i, fd;
	const int N = 100;

	(void)snprintf(path, sizeof(path), "%s/wc_rt", dir);
	fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) {
		printf("WCRESULT rt SKIP open-failed\n");
		fflush(stdout);
		return;
	}

	t0 = now_us();
	for (i = 0; i < N; i++) {
		(void)ftruncate(fd, 64);
	}
	t1 = now_us();
	printf("WCRESULT rt ftruncate     calls=%d per_call_us=%lld\n", N, (t1 - t0) / N);
	fflush(stdout);

	(void)close(fd);

	t0 = now_us();
	for (i = 0; i < N; i++) {
		int f2 = open(path, O_RDONLY);

		if (f2 >= 0) {
			(void)close(f2);
		}
	}
	t1 = now_us();
	printf("WCRESULT rt open+close    calls=%d per_call_us=%lld\n", N, (t1 - t0) / N);
	fflush(stdout);

	(void)unlink(path);
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
	run_op_mix(dir);
	run_alignment(dir);
	run_concurrency(dir);
	run_cpu_vs_wait(dir);
	run_roundtrip(dir);

	printf("WCBENCH done\n");
	fflush(stdout);
	return 0;
}
