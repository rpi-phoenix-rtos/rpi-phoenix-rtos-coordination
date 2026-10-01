/*
 * mallocrate -- malloc+free pair cost, single- and multi-threaded (browser milestone B3).
 *
 * The number behind browser PLAN decision 4 (and the P24 owner question): what one malloc+free
 * costs once a process has threads. build.sh links this twice from the same source:
 *   mallocrate          libphoenix's allocator (a kernel mutex per call when multithreaded)
 *   mallocrate-mimalloc WebKit 2.54's vendored mimalloc, overriding malloc/free as in `jsc`
 *
 * Usage: mallocrate [threads=4] [pairs-per-thread=200000]
 * Output, one line per (phase, size):
 *   MALLOCRATE impl=<name> phase=<single|single-threaded-process|N-threads> size=<bytes> ns/pair=<x>
 * "single" runs before any thread exists; "single-threaded-process" runs on the main thread
 * after one idle helper thread was started (libphoenix switches to its locked path for good).
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifndef MALLOCRATE_IMPL
#define MALLOCRATE_IMPL "libphoenix"
#endif

static const size_t sizes[] = { 16, 64, 256, 2048 };
#define NSIZES (sizeof(sizes) / sizeof(sizes[0]))
#define BATCH  64

static long pairs = 200000;
static volatile uintptr_t sink;


static double now_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}


/* Allocate a batch, touch it, free it: a little more realistic than malloc(n); free(p) in a
 * tight loop, which a thread cache turns into the same block every time. */
static void churn(size_t size, long n)
{
	void *p[BATCH];
	long i;
	int j;

	for (i = 0; i < n; i += BATCH) {
		for (j = 0; j < BATCH; j++) {
			p[j] = malloc(size);
			*(volatile char *)p[j] = (char)j;
		}
		for (j = 0; j < BATCH; j++) {
			sink += (uintptr_t)p[j];
			free(p[j]);
		}
	}
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


static void *idle(void *arg)
{
	(void)arg;
	return NULL;
}


int main(int argc, char **argv)
{
	int threads = (argc > 1) ? atoi(argv[1]) : 4;
	unsigned int s;
	int t;
	double t0, dt;
	pthread_t helper, tid[64];
	worker_arg_t wa[64];

	if (argc > 2) {
		pairs = atol(argv[2]);
	}
	if (threads < 1 || threads > 64 || pairs < BATCH) {
		fprintf(stderr, "usage: mallocrate [threads 1..64] [pairs >= %d]\n", BATCH);
		return 2;
	}

	for (s = 0; s < NSIZES; s++) {
		churn(sizes[s], BATCH);  /* warm */
		t0 = now_ns();
		churn(sizes[s], pairs);
		dt = now_ns() - t0;
		printf("MALLOCRATE impl=%s phase=single size=%zu ns/pair=%.1f\n", MALLOCRATE_IMPL, sizes[s], dt / (double)pairs);
	}

	/* One thread is enough for libphoenix to take its mutex on every call from now on. */
	pthread_create(&helper, NULL, idle, NULL);
	pthread_join(helper, NULL);
	for (s = 0; s < NSIZES; s++) {
		t0 = now_ns();
		churn(sizes[s], pairs);
		dt = now_ns() - t0;
		printf("MALLOCRATE impl=%s phase=single-threaded-process size=%zu ns/pair=%.1f\n", MALLOCRATE_IMPL, sizes[s], dt / (double)pairs);
	}

	for (s = 0; s < NSIZES; s++) {
		pthread_barrier_t start;
		pthread_barrier_init(&start, NULL, (unsigned int)threads + 1u);
		for (t = 0; t < threads; t++) {
			wa[t].size = sizes[s];
			wa[t].start = &start;
			pthread_create(&tid[t], NULL, worker, &wa[t]);
		}
		pthread_barrier_wait(&start);
		t0 = now_ns();
		for (t = 0; t < threads; t++) {
			pthread_join(tid[t], NULL);
		}
		dt = now_ns() - t0;
		pthread_barrier_destroy(&start);
		/* Wall time per pair per thread: equals "single" if the threads scale perfectly. */
		printf("MALLOCRATE impl=%s phase=%d-threads size=%zu ns/pair=%.1f\n", MALLOCRATE_IMPL, threads, sizes[s], dt / (double)pairs);
	}
	return 0;
}
