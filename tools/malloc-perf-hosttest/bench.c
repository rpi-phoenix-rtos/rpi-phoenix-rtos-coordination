/*
 * Host benchmark for libphoenix's allocator (stdlib/malloc_dl.c).
 *
 * Compiles the REAL malloc_dl.c (plus the real sys/ulock.c, sys/rb.c and
 * sys/list.c) for the host and times it with the same workload as
 * tools/browser/jsc/bench/mallocrate.c: batches of 64 malloc()s, each block
 * touched, then 64 free()s. Next to the time it counts what the allocator asked
 * the kernel for -- mmap(), munmap(), va2pa() and page faults per 1000 pairs --
 * because on the Pi those are what the time is made of, and a host run cannot
 * reproduce their price, only their number.
 *
 * Phases, per size:
 *   single   the process has never had a thread: the heap lock is skipped
 *   locked   __libc_multithreaded = 1, one thread: every call takes the lock
 *   Nthr     N threads churning at once (wall time per pair per thread)
 * and a large-block loop: malloc(3 MiB) + memset + free, ns and faults per
 * iteration, with a fixed size and with two alternating sizes.
 *
 * Usage: bench [label] [pairs=200000] [threads=4]
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define _GNU_SOURCE

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limits.h>
#include <errno.h>
#include <unistd.h>
#include <sysexits.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/resource.h>


int __libc_multithreaded;
unsigned long mph_va2paCalls;
static unsigned long mph_mmapCalls;
static unsigned long mph_munmapCalls;


static void *mph_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
	__atomic_fetch_add(&mph_mmapCalls, 1UL, __ATOMIC_RELAXED);
	return mmap(addr, len, prot, flags, fd, off);
}


static int mph_munmap(void *addr, size_t len)
{
	__atomic_fetch_add(&mph_munmapCalls, 1UL, __ATOMIC_RELAXED);
	return munmap(addr, len);
}


#define mmap               mph_mmap
#define munmap             mph_munmap
#define malloc             phx_malloc
#define calloc             phx_calloc
#define realloc            phx_realloc
#define reallocf           phx_reallocf
#define free               phx_free
#define malloc_usable_size phx_malloc_usable_size
#define mallocInfo         phx_mallocInfo
#define _malloc_init       phx_malloc_init
#define malloc_test        phx_malloc_test

#ifndef MPH_SRC
#define MPH_SRC "../../sources/libphoenix/stdlib/malloc_dl.c"
#endif
#include MPH_SRC

#undef mmap
#undef munmap


#define BATCH 64

static const char *label = "libphoenix";
static long pairs = 200000;
static volatile uintptr_t sink;


static double now_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}


static long minflt(void)
{
	struct rusage ru;

	getrusage(RUSAGE_SELF, &ru);
	return ru.ru_minflt;
}


static void churn(size_t size, long n)
{
	void *p[BATCH];
	long i;
	int j;

	for (i = 0; i < n; i += BATCH) {
		for (j = 0; j < BATCH; j++) {
			p[j] = phx_malloc(size);
			*(volatile char *)p[j] = (char)j;
		}
		for (j = 0; j < BATCH; j++) {
			sink += (uintptr_t)p[j];
			phx_free(p[j]);
		}
	}
}


typedef struct {
	unsigned long mmaps, munmaps, va2pas;
	long flt;
	double t;
} snap_t;


static void snap(snap_t *s)
{
	s->mmaps = mph_mmapCalls;
	s->munmaps = mph_munmapCalls;
	s->va2pas = mph_va2paCalls;
	s->flt = minflt();
	s->t = now_ns();
}


static void report(const char *phase, size_t size, const snap_t *a, const snap_t *b, double npairs, double nsPerPair)
{
	printf("MPH impl=%s phase=%s size=%zu ns/pair=%.1f mmap/kpair=%.1f munmap/kpair=%.1f va2pa/kpair=%.1f minflt/kpair=%.1f\n",
		label, phase, size, nsPerPair,
		1000.0 * (double)(b->mmaps - a->mmaps) / npairs,
		1000.0 * (double)(b->munmaps - a->munmaps) / npairs,
		1000.0 * (double)(b->va2pas - a->va2pas) / npairs,
		1000.0 * (double)(b->flt - a->flt) / npairs);
}


/* Best of five, so a scheduling hiccup on the host does not land in the table. */
static int wanted(const char *phase, size_t size)
{
	const char *only = getenv("MPH_ONLY");
	char key[48];

	if (only == NULL) {
		return 1;
	}
	snprintf(key, sizeof(key), "%s:%zu", phase, size);
	return (strcmp(only, key) == 0) ? 1 : 0;
}


static void phaseSerial(const char *phase, size_t size)
{
	snap_t a, b, ba, bb;
	double best = 1e300;
	int r;

	if ((strncmp(phase, "live", 4) != 0) && (wanted(phase, size) == 0)) {
		return;
	}

	churn(size, BATCH);
	for (r = 0; r < 5; r++) {
		snap(&a);
		churn(size, pairs);
		snap(&b);
		if ((b.t - a.t) < best) {
			best = b.t - a.t;
			ba = a;
			bb = b;
		}
	}
	report(phase, size, &ba, &bb, (double)pairs, best / (double)pairs);
}


typedef struct {
	size_t size;
	pthread_barrier_t *start;
} worker_arg_t;


static void *worker(void *arg)
{
	worker_arg_t *w = arg;

	pthread_barrier_wait(w->start);
	churn(w->size, pairs);
	return NULL;
}


static void phaseThreads(int threads, size_t size)
{
	pthread_t tid[64];
	worker_arg_t wa[64];
	pthread_barrier_t start;
	snap_t a, b;
	char phase[32];
	int t;

	snprintf(phase, sizeof(phase), "%dthr", threads);
	if (wanted(phase, size) == 0) {
		return;
	}
	pthread_barrier_init(&start, NULL, (unsigned int)threads + 1u);
	for (t = 0; t < threads; t++) {
		wa[t].size = size;
		wa[t].start = &start;
		pthread_create(&tid[t], NULL, worker, &wa[t]);
	}
	pthread_barrier_wait(&start);
	snap(&a);
	for (t = 0; t < threads; t++) {
		pthread_join(tid[t], NULL);
	}
	snap(&b);
	pthread_barrier_destroy(&start);
	report(phase, size, &a, &b, (double)pairs * (double)threads, (b.t - a.t) / (double)pairs);
}


/* The P26 workload: a frame-sized buffer allocated, filled and freed per frame.
 * With `alt`, every other frame asks for a slightly different size, as a decoder
 * whose output size varies does. */
static void phaseBig(size_t size, size_t alt, long iters)
{
	snap_t a, b;
	long i;

	if (wanted("big", size) == 0) {
		return;
	}
	snap(&a);
	for (i = 0; i < iters + 2; i++) {
		size_t sz = (((i & 1) != 0) && (alt != 0)) ? alt : size;
		unsigned char *p = phx_malloc(sz);

		if (p == NULL) {
			printf("MPH big: malloc(%zu) failed\n", sz);
			exit(1);
		}
		memset(p, (int)i, sz);
		sink += p[sz / 2];
		phx_free(p);
		if (i == 1) {
			snap(&a); /* skip the first two: they create the heap(s) */
		}
	}
	snap(&b);
	printf("MPH impl=%s phase=big size=%zu alt=%zu ns/iter=%.0f mmap/iter=%.2f munmap/iter=%.2f minflt/iter=%.1f\n",
		label, size, alt, (b.t - a.t) / (double)iters,
		(double)(b.mmaps - a.mmaps) / (double)iters,
		(double)(b.munmaps - a.munmaps) / (double)iters,
		(double)(b.flt - a.flt) / (double)iters);
}


/* Real processes hold many heaps at once (the C1 hunt counted ~200 in
 * SuperTuxKart), and some per-call costs scale with that number. Hold `nheaps`
 * blocks big enough to get a heap each, churn small blocks, release them. */
static void phaseLoaded(int nheaps, size_t size)
{
	static void *held[512];
	unsigned long m0 = mph_mmapCalls;
	char phase[32];
	int i;

	snprintf(phase, sizeof(phase), "live%d", nheaps);
	if (wanted(phase, size) == 0) {
		return;
	}
	for (i = 0; i < nheaps; i++) {
		held[i] = phx_malloc(40000);
	}
	if ((int)(mph_mmapCalls - m0) < nheaps) {
		printf("MPH loaded: only %lu heaps for %d blocks\n", mph_mmapCalls - m0, nheaps);
	}
	phaseSerial(phase, size);
	for (i = 0; i < nheaps; i++) {
		phx_free(held[i]);
	}
}


int main(int argc, char **argv)
{
	static const size_t sizes[] = { 16, 64, 256, 2048 };
	int threads = 4;
	unsigned int s;

	if (argc > 1) {
		label = argv[1];
	}
	if (argc > 2) {
		pairs = atol(argv[2]);
	}
	if (argc > 3) {
		threads = atoi(argv[3]);
	}
	if ((pairs < BATCH) || (threads < 1) || (threads > 64)) {
		fprintf(stderr, "usage: bench [label] [pairs >= %d] [threads 1..64]\n", BATCH);
		return 2;
	}

	phx_malloc_init();

	phaseBig(3u << 20, 0, 200);
	phaseBig(3u << 20, (3u << 20) + (100u << 10), 200);

	for (s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
		phaseSerial("single", sizes[s]);
	}

	phaseLoaded(200, 16);
	phaseLoaded(200, 64);

	__libc_multithreaded = 1;
	for (s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
		phaseSerial("locked", sizes[s]);
	}
	for (s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
		phaseThreads(threads, sizes[s]);
	}

	return 0;
}
