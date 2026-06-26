/*
 * Phoenix-RTOS Raspberry Pi 4 — CORE stress / micro-benchmark util (task #38).
 *
 * Exercises the kernel's core services — virtual memory, threading, the
 * scheduler and the syscall fast-path — and classifies every outcome into the
 * three buckets defined by tools/stress/stress.h:
 *
 *   OK     succeeded as intended
 *   LIMIT  the OS correctly refused at a resource boundary (this is correct,
 *          NOT a finding) — malloc()->NULL near full RAM, beginthreadex()->
 *          -EAGAIN at the thread cap, etc.
 *   FAULT  a real defect — crash, hang (heartbeat stops), corruption
 *          (checksum mismatch), wrong result (lost update), or an errno that
 *          is not a legitimate resource limit.
 *
 * HOST: this file is cross-compiled static for aarch64-phoenix and staged onto
 * the NFS rootfs; it is run on the Pi from psh. See build-stress-core.sh.
 *
 * NOTE on the scheduler: the Pi 4 port currently runs a cpu0-only scheduler
 * (see SMP D-7/D-8), so the `thread`/`sched` modes exercise PREEMPTION races
 * and fairness on a single CPU, NOT true SMP parallelism.
 *
 * Copyright 2026 Phoenix Systems
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/threads.h>
#include <sys/time.h>

#include "stress.h"

/* aarch64 page size — meminfo page byte-counts and the write-touch step align here. */
#define PAGE_SIZE 0x1000u

/* Threads must outlive their stacks; #120/#152 history: 8KB pool stacks overflowed
 * the Pi 4 fs call chain. Keep CORE-stress thread stacks comfortably above that. */
#define THREAD_STACK_SIZE (32u * 1024u)

/* threadJoin() timeout is in microseconds (kernel proc_join: now + timeout); a
 * return of -ETIME means the join timed out. 0 == wait forever, which we never
 * use because a hung thread would then wedge the whole run with no localization. */
#define JOIN_TIMEOUT_US (30ull * 1000ull * 1000ull) /* 30 s — generous, finite */

/* Progress-aware join for the high-contention counter test: a fixed timeout is
 * the WRONG deadlock test there, because a large, fully-serialized workload on
 * the cpu0-only scheduler can legitimately take minutes. Instead we poll the
 * join in short slices and watch the shared counter: if it is still climbing,
 * the work is just slow (NOT a fault); only a counter that is frozen across a
 * full stall window is a genuine deadlock/wedge. */
#define JOIN_POLL_US      (2ull * 1000ull * 1000ull) /* 2 s per poll slice + heartbeat cadence */
#define STALL_WINDOW_US   (30ull * 1000ull * 1000ull) /* counter must be frozen this long = deadlock */

/* Cap total counter work so a default-ish `all` run can't spin for many minutes
 * on one core. perThread is clamped so nthreads*perThread <= this. */
#define MAX_COUNTER_OPS   (8ul * 1000ul * 1000ul) /* 8M serialized lock/unlock ops */

#define MAX_THREADS 64u


/* ---- shared meminfo helper -------------------------------------------------
 * Returns free RAM in bytes. The page.{alloc,free,boot} fields are byte counts
 * (kernel vm/page.c sets them from allocsz/totalsz). All four mapsz fields MUST
 * be -1 (and the struct zeroed) so the kernel does not try to fill the page/
 * entry maps into NULL pointers (idiom copied verbatim from psh mem.c). */
static unsigned int sr_free_ram(void)
{
	meminfo_t info;

	memset(&info, 0, sizeof(info));
	info.page.mapsz = -1;
	info.entry.mapsz = -1;
	info.entry.kmapsz = -1;
	info.maps.mapsz = -1;

	meminfo(&info);
	return info.page.free;
}


/* Monotonic wall-clock in microseconds — used for throughput timing and the
 * progress-aware join's stall window. */
static uint64_t now_us(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull;
}


static unsigned int sr_alloc_ram(void)
{
	meminfo_t info;

	memset(&info, 0, sizeof(info));
	info.page.mapsz = -1;
	info.entry.mapsz = -1;
	info.entry.kmapsz = -1;
	info.maps.mapsz = -1;

	meminfo(&info);
	return info.page.alloc;
}


/* A trivial reproducible PRNG so a run is deterministic across hosts/boots and
 * the orchestrator can diff numbers. */
static unsigned long sr_rng_state = 0x12345678ul;

static unsigned long sr_rng(void)
{
	sr_rng_state = sr_rng_state * 1103515245ul + 12345ul;
	return (sr_rng_state >> 8) & 0x7ffffffful;
}


/* Fill a buffer with a position+seed-derived pattern and verify it later. A
 * mismatch is memory corruption — a genuine FAULT. */
static void sr_fill(unsigned char *p, size_t n, unsigned char seed)
{
	size_t i;
	for (i = 0; i < n; i++) {
		p[i] = (unsigned char)(seed + (unsigned char)(i * 31u + (i >> 8)));
	}
}


static int sr_verify(const unsigned char *p, size_t n, unsigned char seed)
{
	size_t i;
	for (i = 0; i < n; i++) {
		if (p[i] != (unsigned char)(seed + (unsigned char)(i * 31u + (i >> 8)))) {
			return (int)i; /* >=0 : first bad byte */
		}
	}
	return -1; /* clean */
}


/* ===========================================================================
 * MODE: mem
 * (a) malloc/free churn with pattern verify (corruption hunt)
 * (b) near-full-RAM watermark via write-touched incremental chunks
 * (c) leak/fragmentation TREND over many cycles (orchestrator reads the trend)
 * intensity (argv[2]) scales all three; default modest so `all` won't wedge the
 * shared box. Watermark-hunting (b) is bounded by intensity, not unconditional.
 * ===========================================================================*/

static int mode_mem(unsigned long intensity)
{
	sr_tally_t t = { 0 };
	const char *suite = "mem";

	/* ---- (a) malloc/free churn ---- */
	{
		unsigned long churn = 200ul * intensity;
		unsigned long i;
		unsigned long corrupt = 0, allocFails = 0, reallocs = 0;
		size_t maxSz = 1024u * 1024u; /* 1 MB upper bound */

		SR_HB(suite, "churn.start n=%lu", churn);
		for (i = 0; i < churn; i++) {
			size_t sz = 16u + (size_t)(sr_rng() % (maxSz - 16u + 1u));
			unsigned char seed = (unsigned char)sr_rng();
			unsigned char *p = malloc(sz);

			if (p == NULL) {
				/* malloc returning NULL is the allocator correctly refusing. */
				allocFails++;
				continue;
			}
			sr_fill(p, sz, seed);

			/* periodically realloc (grow or shrink) before verifying */
			if ((i % 7u) == 0u) {
				size_t nsz = 16u + (size_t)(sr_rng() % (maxSz - 16u + 1u));
				unsigned char *q = realloc(p, nsz);
				if (q != NULL) {
					/* realloc preserves min(old,new) bytes — verify that prefix */
					size_t keep = (nsz < sz) ? nsz : sz;
					int bad = sr_verify(q, keep, seed);
					if (bad >= 0) {
						corrupt++;
						SR_FAULT(&t, suite, "churn.realloc",
							"corruption after realloc sz=%zu->%zu firstbad=%d", sz, nsz, bad);
					}
					reallocs++;
					free(q);
					continue;
				}
				/* realloc failed: original block is still valid, fall through */
			}

			int bad = sr_verify(p, sz, seed);
			if (bad >= 0) {
				corrupt++;
				SR_FAULT(&t, suite, "churn.verify", "corruption sz=%zu firstbad=%d", sz, bad);
			}
			free(p);

			if ((i % 2000u) == 0u) {
				SR_HB(suite, "churn.progress i=%lu", i);
			}
		}
		if (corrupt == 0) {
			SR_OK(&t, suite, "churn", "iters=%lu reallocs=%lu allocNull=%lu free=%uKB",
				churn, reallocs, allocFails, sr_free_ram() / 1024u);
		}
	}

	/* ---- (b) near-full-RAM watermark ---- */
	{
		/* Step size and step cap derive from intensity. We allocate chunks and
		 * write-touch every page (Phoenix lazy-maps; untouched pages cost
		 * nothing). A clean malloc->NULL is the LIMIT we want to RECORD; a
		 * crash/wedge here is a FAULT and the heartbeat localizes it. */
		size_t chunkSz = (size_t)(8u * 1024u * 1024u); /* 8 MB per step */
		unsigned int maxSteps = (unsigned int)(8ul * intensity);
		if (maxSteps < 4u) {
			maxSteps = 4u;
		}
		if (maxSteps > 512u) {
			maxSteps = 512u; /* hard ceiling so `all` never tries to eat unbounded RAM */
		}

		void **chunks = calloc(maxSteps, sizeof(void *));
		unsigned int got = 0;
		unsigned int freeBefore = sr_free_ram();
		int hitLimit = 0;
		int corruptStep = 0;

		SR_HB(suite, "watermark.start freeKB=%u chunk=%zuKB maxSteps=%u",
			freeBefore / 1024u, chunkSz / 1024u, maxSteps);

		if (chunks == NULL) {
			SR_FAULT(&t, suite, "watermark", "bookkeeping calloc failed (errno=%d)", errno);
		}
		else {
			unsigned int step;
			for (step = 0; step < maxSteps; step++) {
				unsigned char *p = malloc(chunkSz);
				SR_HB(suite, "watermark.step=%u alloc=%p freeKB=%u",
					step, (void *)p, sr_free_ram() / 1024u);
				if (p == NULL) {
					/* The OS refused at the RAM boundary — this is the watermark. */
					hitLimit = 1;
					SR_LIMIT(&t, suite, "watermark",
						"malloc->NULL at step=%u touchedKB=%u freeBeforeKB=%u freeNowKB=%u (errno=%d)",
						step, step * (unsigned int)(chunkSz / 1024u), freeBefore / 1024u,
						sr_free_ram() / 1024u, errno);
					break;
				}

				/* write-touch every page so the mapping is actually backed */
				unsigned char seed = (unsigned char)(0xa5u + step);
				size_t off;
				for (off = 0; off < chunkSz; off += PAGE_SIZE) {
					p[off] = (unsigned char)(seed + (unsigned char)(off >> 12));
				}
				chunks[step] = p;
				got++;
			}

			if (!hitLimit) {
				/* Never reached NULL within the step cap — fine, just report
				 * how far we got. NOT a fault and NOT a real limit. */
				SR_OK(&t, suite, "watermark",
					"no-NULL within cap steps=%u touchedKB=%u freeNowKB=%u",
					got, got * (unsigned int)(chunkSz / 1024u), sr_free_ram() / 1024u);
			}

			/* verify the first byte we wrote in each surviving chunk, then free */
			unsigned int j;
			for (j = 0; j < got; j++) {
				unsigned char *p = chunks[j];
				unsigned char want = (unsigned char)(0xa5u + j);
				if (p != NULL && p[0] != want) {
					corruptStep++;
					SR_FAULT(&t, suite, "watermark.verify",
						"corruption in chunk %u: got=0x%02x want=0x%02x", j, p[0], want);
				}
				free(p);
			}
			free(chunks);

			/* confirm recovery: free RAM should return near the pre-test level */
			unsigned int freeAfter = sr_free_ram();
			SR_HB(suite, "watermark.recovered freeBeforeKB=%u freeAfterKB=%u",
				freeBefore / 1024u, freeAfter / 1024u);
			if (corruptStep == 0) {
				SR_OK(&t, suite, "watermark.recover",
					"freeBeforeKB=%u freeAfterKB=%u (recovery delta is informational, not a leak)",
					freeBefore / 1024u, freeAfter / 1024u);
			}
		}
	}

	/* ---- (c) leak / fragmentation TREND ---- */
	{
		/* Run the SAME alloc/free cycle many times, printing free RAM every K
		 * iters. We do NOT decide leak-vs-plateau here — we emit the series and
		 * let the orchestrator look for MONOTONIC decline (real leak) vs a
		 * stable plateau (arena retention = not a leak). A single before/after
		 * delta is explicitly NOT treated as a leak. */
		unsigned long iters = 300ul * intensity;
		unsigned long k = 50ul; /* report cadence */
		unsigned long i;
		size_t fixed = 64u * 1024u; /* same shape every iter */

		SR_HB(suite, "trend.start iters=%lu reportEvery=%lu", iters, k);
		printf("STRESS mem.trend SERIES iter freeKB allocKB\n");
		for (i = 0; i < iters; i++) {
			/* allocate a small fan of blocks then free them all — identical
			 * each iteration so the steady-state free level should be flat */
			void *a = malloc(fixed);
			void *b = malloc(fixed / 2u);
			void *c = malloc(fixed / 4u);
			if (a != NULL) {
				memset(a, (int)i, fixed);
			}
			if (b != NULL) {
				memset(b, (int)i, fixed / 2u);
			}
			if (c != NULL) {
				memset(c, (int)i, fixed / 4u);
			}
			free(b);
			free(a);
			free(c);

			if ((i % k) == 0u) {
				printf("STRESS mem.trend SERIES %lu %u %u\n", i, sr_free_ram() / 1024u, sr_alloc_ram() / 1024u);
				SR_HB(suite, "trend.i=%lu", i);
			}
		}
		printf("STRESS mem.trend SERIES %lu %u %u\n", iters, sr_free_ram() / 1024u, sr_alloc_ram() / 1024u);
		SR_OK(&t, suite, "trend",
			"iters=%lu — inspect SERIES lines for monotonic decline (leak) vs plateau (arena retention)",
			iters);
	}

	SR_SUMMARY(&t, suite, "freeKB=%u", sr_free_ram() / 1024u);
	return (t.fault != 0) ? 1 : 0;
}


/* ===========================================================================
 * MODE: thread
 * (1) N threads each doing M mutex-protected increments of a shared counter;
 *     join all with a TIMEOUT (timeout = deadlock = FAULT). Final must == N*M.
 * (2) condvar producer/consumer ping-pong.
 * ===========================================================================*/

typedef struct {
	handle_t lock;
	volatile unsigned long counter;
	unsigned long perThread;
	volatile unsigned int started;
	volatile unsigned int finished;
} thread_ctx_t;

static thread_ctx_t g_tctx;

static void incr_thread(void *arg)
{
	(void)arg;
	unsigned long i;
	for (i = 0; i < g_tctx.perThread; i++) {
		mutexLock(g_tctx.lock);
		g_tctx.counter++;
		mutexUnlock(g_tctx.lock);
	}
	endthread();
}

/* condvar ping-pong: producer sets a slot full, consumer drains it. */
typedef struct {
	handle_t lock;
	handle_t cvFull;
	handle_t cvEmpty;
	volatile int full;
	volatile unsigned long produced;
	volatile unsigned long consumed;
	unsigned long rounds;
	volatile int done;
} pingpong_ctx_t;

static pingpong_ctx_t g_pp;

static void consumer_thread(void *arg)
{
	(void)arg;
	unsigned long n;
	for (n = 0; n < g_pp.rounds; n++) {
		mutexLock(g_pp.lock);
		while (g_pp.full == 0) {
			condWait(g_pp.cvFull, g_pp.lock, 0);
		}
		g_pp.full = 0;
		g_pp.consumed++;
		condSignal(g_pp.cvEmpty);
		mutexUnlock(g_pp.lock);
	}
	g_pp.done = 1;
	endthread();
}

static int mode_thread(unsigned long intensity)
{
	sr_tally_t t = { 0 };
	const char *suite = "thread";

	/* ---- (1) shared-counter race ---- */
	{
		unsigned int nthreads = (unsigned int)(4ul + 2ul * intensity);
		if (nthreads > MAX_THREADS) {
			nthreads = MAX_THREADS;
		}
		unsigned long perThread = 20000ul * intensity;
		/* Clamp total work so a high-intensity `all` run stays bounded: with N
		 * threads all serialized on one mutex on the cpu0-only scheduler, total
		 * ops = nthreads*perThread. Keep that under MAX_COUNTER_OPS. */
		if ((unsigned long)nthreads * perThread > MAX_COUNTER_OPS) {
			perThread = MAX_COUNTER_OPS / nthreads;
			if (perThread == 0ul) {
				perThread = 1ul;
			}
		}

		/* Heap-allocate per-thread stacks; they must OUTLIVE the threads
		 * (freed only after join). 16-byte aligned via dedicated alloc. */
		unsigned char **stacks = calloc(nthreads, sizeof(unsigned char *));
		handle_t *tids = calloc(nthreads, sizeof(handle_t));
		int spawnFail = 0, limited = 0;
		unsigned int spawned = 0;

		if (stacks == NULL || tids == NULL || mutexCreate(&g_tctx.lock) < 0) {
			SR_FAULT(&t, suite, "counter.setup", "alloc/mutexCreate failed (errno=%d)", errno);
			free(stacks);
			free(tids);
		}
		else {
			g_tctx.counter = 0;
			g_tctx.perThread = perThread;

			SR_HB(suite, "counter.spawn n=%u perThread=%lu", nthreads, perThread);
			unsigned int i;
			for (i = 0; i < nthreads; i++) {
				stacks[i] = malloc(THREAD_STACK_SIZE);
				if (stacks[i] == NULL) {
					limited = 1;
					break;
				}
				int rc = beginthreadex(incr_thread, 4, stacks[i], THREAD_STACK_SIZE, NULL, &tids[i]);
				if (rc < 0) {
					free(stacks[i]);
					stacks[i] = NULL;
					/* -EAGAIN at the thread cap is a legitimate LIMIT. */
					if (sr_errno_is_limit(-rc) || sr_errno_is_limit(errno)) {
						limited = 1;
					}
					else {
						spawnFail = 1;
						SR_FAULT(&t, suite, "counter.spawn",
							"beginthreadex rc=%d errno=%d (not a resource limit)", rc, errno);
					}
					break;
				}
				spawned++;
			}

			if (limited && !spawnFail) {
				SR_LIMIT(&t, suite, "counter.spawn",
					"thread/stack cap reached after %u of %u (rc/errno indicated a resource limit)",
					spawned, nthreads);
			}

			/* Progress-aware join. A plain fixed-timeout join is a false-positive
			 * generator here: a large fully-serialized workload on the cpu0-only
			 * scheduler can legitimately take minutes. So we poll each thread in
			 * short slices, emit a heartbeat with the LIVE counter each slice, and
			 * only declare a deadlock if the counter is FROZEN across a full stall
			 * window. A counter that is still climbing = slow, not a fault. */
			int deadlock = 0;
			unsigned long expectTotal = (unsigned long)spawned * perThread;
			SR_HB(suite, "counter.join spawned=%u expectTotal=%lu", spawned, expectTotal);
			for (i = 0; i < spawned && !deadlock; i++) {
				unsigned long lastSeen = g_tctx.counter;
				uint64_t lastProgressUs = now_us();
				for (;;) {
					int jr = threadJoin((int)tids[i], JOIN_POLL_US);
					if (jr != -ETIME) {
						break; /* thread reaped (or a non-timeout error) — done with it */
					}
					/* still running after this slice — check the counter trend */
					unsigned long cur = g_tctx.counter;
					uint64_t nowU = now_us();
					if (cur != lastSeen) {
						/* work is progressing — reset the stall clock, keep waiting */
						lastSeen = cur;
						lastProgressUs = nowU;
						SR_HB(suite, "counter.progress thread=%u counter=%lu/%lu",
							i, cur, expectTotal);
					}
					else if ((nowU - lastProgressUs) >= STALL_WINDOW_US) {
						/* counter frozen for the whole stall window = genuine wedge */
						deadlock = 1;
						SR_FAULT(&t, suite, "counter.join",
							"DEADLOCK: counter frozen at %lu/%lu for >%llus while joining thread %u",
							cur, expectTotal,
							(unsigned long long)(STALL_WINDOW_US / 1000000ull), i);
						break;
					}
					else {
						SR_HB(suite, "counter.stall thread=%u counter=%lu (no advance, %llus into window)",
							i, cur, (unsigned long long)((nowU - lastProgressUs) / 1000000ull));
					}
				}
			}

			/* Free stacks only AFTER join. On a genuine deadlock the wedged
			 * threads are still alive, so freeing their stacks would be a
			 * use-after-free — leave those unfreed (the run is reporting a FAULT
			 * and exiting anyway); free only on the clean path. */
			if (!deadlock) {
				for (i = 0; i < nthreads; i++) {
					free(stacks[i]);
				}
			}

			unsigned long expect = (unsigned long)spawned * perThread;
			if (!deadlock && !spawnFail) {
				if (g_tctx.counter == expect) {
					SR_OK(&t, suite, "counter",
						"threads=%u perThread=%lu total=%lu (exact, no lost updates)",
						spawned, perThread, g_tctx.counter);
				}
				else {
					/* counter != N*M means a mutex failed to serialize — a real
					 * lost-update race, which is a FAULT. */
					SR_FAULT(&t, suite, "counter",
						"lost-update race: counter=%lu expected=%lu (diff=%ld)",
						g_tctx.counter, expect, (long)(expect - g_tctx.counter));
				}
			}

			resourceDestroy(g_tctx.lock);
			free(stacks);
			free(tids);
		}
	}

	/* ---- (2) condvar producer/consumer ping-pong ---- */
	{
		unsigned long rounds = 5000ul * intensity;
		unsigned char *cstack = malloc(THREAD_STACK_SIZE);
		handle_t ctid = 0;
		int ok = 1;

		memset(&g_pp, 0, sizeof(g_pp));
		g_pp.rounds = rounds;

		if (cstack == NULL || mutexCreate(&g_pp.lock) < 0 ||
				condCreate(&g_pp.cvFull) < 0 || condCreate(&g_pp.cvEmpty) < 0) {
			SR_FAULT(&t, suite, "pingpong.setup", "alloc/mutex/cond create failed (errno=%d)", errno);
			ok = 0;
		}

		if (ok) {
			SR_HB(suite, "pingpong.start rounds=%lu", rounds);
			int rc = beginthreadex(consumer_thread, 4, cstack, THREAD_STACK_SIZE, NULL, &ctid);
			if (rc < 0) {
				SR_FAULT(&t, suite, "pingpong.spawn", "consumer beginthreadex rc=%d errno=%d", rc, errno);
				ok = 0;
			}
		}

		if (ok) {
			/* producer runs on the main thread */
			unsigned long n;
			for (n = 0; n < rounds; n++) {
				mutexLock(g_pp.lock);
				while (g_pp.full != 0) {
					condWait(g_pp.cvEmpty, g_pp.lock, 0);
				}
				g_pp.full = 1;
				g_pp.produced++;
				condSignal(g_pp.cvFull);
				mutexUnlock(g_pp.lock);
				if ((n % 1000u) == 0u) {
					SR_HB(suite, "pingpong.produced=%lu", n);
				}
			}

			int jr = threadJoin((int)ctid, JOIN_TIMEOUT_US);
			if (jr == -ETIME) {
				SR_FAULT(&t, suite, "pingpong.join", "consumer join TIMED OUT (deadlock)");
			}
			else if (g_pp.produced != rounds || g_pp.consumed != rounds) {
				SR_FAULT(&t, suite, "pingpong",
					"count mismatch produced=%lu consumed=%lu rounds=%lu",
					g_pp.produced, g_pp.consumed, rounds);
			}
			else {
				SR_OK(&t, suite, "pingpong",
					"rounds=%lu produced=%lu consumed=%lu (condvar handoff intact)",
					rounds, g_pp.produced, g_pp.consumed);
			}
		}

		free(cstack);
		if (g_pp.lock != 0) {
			resourceDestroy(g_pp.lock);
		}
		if (g_pp.cvFull != 0) {
			resourceDestroy(g_pp.cvFull);
		}
		if (g_pp.cvEmpty != 0) {
			resourceDestroy(g_pp.cvEmpty);
		}
	}

	SR_SUMMARY(&t, suite, "(cpu0-only sched: preemption races, not SMP)");
	return (t.fault != 0) ? 1 : 0;
}


/* ===========================================================================
 * MODE: sched
 * Many threads each spin-counting for a fixed wall-time; report per-thread
 * progress spread (fairness) and max gap (jitter). A thread getting ~0 progress
 * is gross starvation = FAULT.
 * ===========================================================================*/

typedef struct {
	volatile unsigned long counts[MAX_THREADS];
	volatile unsigned int go;
	volatile unsigned int stop;
} sched_ctx_t;

static sched_ctx_t g_sched;

static void spin_thread(void *arg)
{
	unsigned int idx = (unsigned int)(uintptr_t)arg;
	while (g_sched.go == 0) {
		/* busy-wait for the start gun */
	}
	while (g_sched.stop == 0) {
		g_sched.counts[idx]++;
	}
	endthread();
}

static int mode_sched(unsigned long intensity)
{
	sr_tally_t t = { 0 };
	const char *suite = "sched";

	unsigned int nthreads = (unsigned int)(4ul + 2ul * intensity);
	if (nthreads > MAX_THREADS) {
		nthreads = MAX_THREADS;
	}
	uint64_t runUs = 2000000ull; /* 2 s of wall-clock counting */

	unsigned char **stacks = calloc(nthreads, sizeof(unsigned char *));
	handle_t *tids = calloc(nthreads, sizeof(handle_t));
	unsigned int spawned = 0;

	memset(&g_sched, 0, sizeof(g_sched));

	if (stacks == NULL || tids == NULL) {
		SR_FAULT(&t, suite, "setup", "alloc failed (errno=%d)", errno);
		free(stacks);
		free(tids);
		SR_SUMMARY(&t, suite, "");
		return 1;
	}

	SR_HB(suite, "spawn n=%u runUs=%llu", nthreads, (unsigned long long)runUs);
	unsigned int i;
	for (i = 0; i < nthreads; i++) {
		stacks[i] = malloc(THREAD_STACK_SIZE);
		if (stacks[i] == NULL) {
			break;
		}
		int rc = beginthreadex(spin_thread, 4, stacks[i], THREAD_STACK_SIZE, (void *)(uintptr_t)i, &tids[i]);
		if (rc < 0) {
			free(stacks[i]);
			stacks[i] = NULL;
			if (sr_errno_is_limit(-rc) || sr_errno_is_limit(errno)) {
				SR_LIMIT(&t, suite, "spawn", "thread cap reached after %u of %u", spawned, nthreads);
			}
			else {
				SR_FAULT(&t, suite, "spawn", "beginthreadex rc=%d errno=%d (not a limit)", rc, errno);
			}
			break;
		}
		spawned++;
	}

	if (spawned == 0) {
		SR_FAULT(&t, suite, "spawn", "no threads spawned");
		free(stacks);
		free(tids);
		SR_SUMMARY(&t, suite, "");
		return 1;
	}

	/* fire the start gun, sleep the run window, then signal stop */
	uint64_t t0 = now_us();
	g_sched.go = 1;
	usleep((useconds_t)runUs);
	g_sched.stop = 1;
	uint64_t elapsed = now_us() - t0;

	int deadlock = 0;
	SR_HB(suite, "join spawned=%u", spawned);
	for (i = 0; i < spawned; i++) {
		int jr = threadJoin((int)tids[i], JOIN_TIMEOUT_US);
		if (jr == -ETIME) {
			deadlock = 1;
			SR_FAULT(&t, suite, "join", "threadJoin TIMED OUT on thread %u", i);
		}
	}

	/* fairness / jitter stats */
	unsigned long minC = ~0ul, maxC = 0, sum = 0;
	unsigned int starved = 0;
	for (i = 0; i < spawned; i++) {
		unsigned long c = g_sched.counts[i];
		if (c < minC) {
			minC = c;
		}
		if (c > maxC) {
			maxC = c;
		}
		sum += c;
	}
	unsigned long avg = sum / spawned;

	/* per-thread progress series for the orchestrator */
	printf("STRESS sched.progress SERIES idx count\n");
	for (i = 0; i < spawned; i++) {
		printf("STRESS sched.progress SERIES %u %lu\n", i, g_sched.counts[i]);
		/* gross starvation: the task's bar is "a thread gets ~0". An uneven
		 * but working scheduler (a thread getting a small share) is NOT a
		 * fault — only essentially-zero progress is. Flag <1% of average. */
		if (avg > 0 && g_sched.counts[i] < (avg / 100ul)) {
			starved++;
		}
	}

	/* free per-thread stacks (only after join), then the bookkeeping arrays */
	for (i = 0; i < spawned; i++) {
		free(stacks[i]);
	}
	free(stacks);
	free(tids);

	if (!deadlock) {
		/* spread as a fairness measure: (max-min)/avg */
		unsigned long spreadPct = (avg > 0) ? ((maxC - minC) * 100ul / avg) : 0;
		if (starved > 0) {
			SR_FAULT(&t, suite, "fairness",
				"%u thread(s) starved (~0 progress, <1%% of avg) threads=%u min=%lu max=%lu avg=%lu",
				starved, spawned, minC, maxC, avg);
		}
		else {
			SR_OK(&t, suite, "fairness",
				"threads=%u min=%lu max=%lu avg=%lu spread=%lu%% elapsedUs=%llu",
				spawned, minC, maxC, avg, spreadPct, (unsigned long long)elapsed);
		}
	}

	SR_SUMMARY(&t, suite, "(cpu0-only sched)");
	return (t.fault != 0) ? 1 : 0;
}


/* ===========================================================================
 * MODE: syscall
 * Tight loop of cheap syscalls — ops/sec throughput + stability check.
 * ===========================================================================*/

static int mode_syscall(unsigned long intensity)
{
	sr_tally_t t = { 0 };
	const char *suite = "syscall";

	/* ---- gettid() throughput ---- */
	{
		unsigned long iters = 200000ul * intensity;
		unsigned long i;
		int first = gettid();
		int mismatch = 0;

		SR_HB(suite, "gettid.start iters=%lu", iters);
		uint64_t t0 = now_us();
		for (i = 0; i < iters; i++) {
			int tid = gettid();
			if (tid != first) {
				mismatch++; /* gettid must be stable within one thread */
			}
		}
		uint64_t dt = now_us() - t0;
		if (mismatch != 0) {
			SR_FAULT(&t, suite, "gettid", "gettid returned varying values within one thread (%d times)", mismatch);
		}
		else {
			unsigned long opsPerSec = (dt > 0) ? (unsigned long)((iters * 1000000ull) / dt) : 0;
			SR_OK(&t, suite, "gettid", "iters=%lu us=%llu ops/sec=%lu tid=%d",
				iters, (unsigned long long)dt, opsPerSec, first);
		}
	}

	/* ---- clock_gettime() throughput + monotonicity ---- */
	{
		unsigned long iters = 200000ul * intensity;
		unsigned long i;
		int regress = 0;
		int err = 0;
		struct timespec prev, cur;

		SR_HB(suite, "clock.start iters=%lu", iters);
		if (clock_gettime(CLOCK_MONOTONIC, &prev) != 0) {
			err = 1;
		}
		uint64_t t0 = now_us();
		for (i = 0; i < iters && err == 0; i++) {
			if (clock_gettime(CLOCK_MONOTONIC, &cur) != 0) {
				err = 1;
				break;
			}
			/* CLOCK_MONOTONIC must never go backwards */
			if (cur.tv_sec < prev.tv_sec ||
					(cur.tv_sec == prev.tv_sec && cur.tv_nsec < prev.tv_nsec)) {
				regress++;
			}
			prev = cur;
		}
		uint64_t dt = now_us() - t0;
		if (err != 0) {
			SR_FAULT(&t, suite, "clock", "clock_gettime failed (errno=%d)", errno);
		}
		else if (regress != 0) {
			SR_FAULT(&t, suite, "clock", "CLOCK_MONOTONIC went backwards %d time(s)", regress);
		}
		else {
			unsigned long opsPerSec = (dt > 0) ? (unsigned long)((iters * 1000000ull) / dt) : 0;
			SR_OK(&t, suite, "clock", "iters=%lu us=%llu ops/sec=%lu (monotonic)",
				iters, (unsigned long long)dt, opsPerSec);
		}
	}

	SR_SUMMARY(&t, suite, "");
	return (t.fault != 0) ? 1 : 0;
}


/* ===========================================================================*/

static void usage(const char *argv0)
{
	printf("usage: %s <mem|thread|sched|syscall|all> [intensity]\n", argv0);
	printf("  intensity: positive integer scaling work (default 1 = modest)\n");
}


int main(int argc, char *argv[])
{
	sr_init();

	if (argc < 2) {
		usage(argv[0]);
		return 2;
	}

	unsigned long intensity = 1;
	if (argc >= 3) {
		long v = atol(argv[2]);
		if (v >= 1) {
			intensity = (unsigned long)v;
		}
	}

	const char *mode = argv[1];
	int rc = 0;

	printf("STRESS-CORE start mode=%s intensity=%lu freeKB=%u\n", mode, intensity, sr_free_ram() / 1024u);

	if (strcmp(mode, "mem") == 0) {
		rc = mode_mem(intensity);
	}
	else if (strcmp(mode, "thread") == 0) {
		rc = mode_thread(intensity);
	}
	else if (strcmp(mode, "sched") == 0) {
		rc = mode_sched(intensity);
	}
	else if (strcmp(mode, "syscall") == 0) {
		rc = mode_syscall(intensity);
	}
	else if (strcmp(mode, "all") == 0) {
		rc |= mode_mem(intensity);
		rc |= mode_thread(intensity);
		rc |= mode_sched(intensity);
		rc |= mode_syscall(intensity);
	}
	else {
		usage(argv[0]);
		return 2;
	}

	printf("STRESS-CORE done mode=%s rc=%d freeKB=%u\n", mode, rc, sr_free_ram() / 1024u);
	return rc;
}
