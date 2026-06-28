/*
 * Phoenix-RTOS Raspberry Pi 4 — PROCESS stress / micro-benchmark util (task #39).
 *
 * Exercises the process-management path: fork()+execve() of many child workers,
 * concurrent spawn, waitpid-reaping with per-child exit-code verification, and a
 * rapid spawn/exit churn loop that surfaces a pid/handle leak (monotonic failure
 * onset) or a slowdown.
 *
 * Spawn idiom: vfork() + execve(), mirroring the proven Phoenix pattern
 * (psh/pshapp.c, the X11 launcher pl_phoenix_xlaunch.c). Under vfork() the child
 * shares the parent's address space until exec, so the child does NOTHING but
 * exec (and _exit on failure) — it never touches parent memory. vfork serializes
 * the *spawn* (the parent is suspended until each child execs), but the exec'd
 * workers then run concurrently, which is what "N concurrent children" needs.
 *
 * The worker is THIS SAME binary re-exec'd with argv[1] == "_worker": it does a
 * bounded compute + alloc, then _exit(WORKER_EXIT_CODE). The re-exec path is
 * argv[0], which MUST be absolute (psh launches us as e.g.
 * /bin/stress-proc) — guarded below.
 *
 * Result lines follow tools/stress/stress.h. Three buckets, only FAULT is a
 * finding: fork()->EAGAIN at the per-system process cap is the OS correctly
 * refusing (LIMIT), not a fault. A lost child, a wrong exit code, or a crash is
 * a FAULT.
 *
 *   stress-proc parallel [N]     spawn up to N children concurrently, reap all
 *   stress-proc churn    [ITERS] rapid spawn/exit cycles, watch for leak/slowdown
 *   stress-proc all              parallel (default N) then churn (default ITERS)
 *   stress-proc _worker          (internal) the child worker; not for direct use
 *
 * Host-side build only (build-stress-pfi.sh). Static aarch64-phoenix, libc only.
 *
 * Copyright 2026 Phoenix Systems
 * Author: Witold Bołt
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/types.h>

#include "stress.h"

extern char **environ;

#define SUITE "proc"

/* The exit code the worker reports on success. A reaped child carrying any other
 * code (or that is lost entirely) is a FAULT. Kept well clear of 0/1/127 (libc /
 * exec-failure codes) so a confused exit is unambiguous. */
#define WORKER_EXIT_CODE 73

/* Defaults chosen to stress without wedging the orchestrator's HW run. */
#define DEFAULT_PARALLEL 32
#define DEFAULT_CHURN    200

/* Churn watchdog: once spawns start failing for a non-limit reason, or once the
 * per-spawn time stays past this multiple of the early baseline for several
 * CONSECUTIVE iterations, we stop and report. A real pid/handle leak shows as a
 * monotonic onset (sustained slowdown); a single NFS blip on one execve must NOT
 * trip it, so we require a run of slow iterations AND an absolute floor below
 * which ratios are ignored (sub-ms jitter is meaningless). */
#define CHURN_SLOWDOWN_FACTOR    8
#define CHURN_SLOWDOWN_RUN       8     /* consecutive slow iters before we flag */
#define CHURN_SLOWDOWN_FLOOR_MS  4     /* ignore deltas below this (jitter) */

/* Worker compute/alloc bounds: enough to touch the allocator + run a real (tiny)
 * loop so the child is not a no-op exec, but bounded so it always terminates. */
#define WORKER_ALLOC_BYTES (64u * 1024u)
#define WORKER_LOOP_ITERS  200000u


static long ms_now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}


/*
 * The worker: a bounded compute + heap touch, then _exit with the agreed code.
 * Deliberately self-contained (no shared state) so concurrent copies can't
 * interfere. A failed alloc here is NOT a process-management fault, so the worker
 * still exits with the success code (the allocator is stress-core's concern); the
 * point of stress-proc is that the PARENT spawns, the child RUNS, and the exit
 * code is delivered intact.
 */
static int run_worker(void)
{
	volatile unsigned long acc = 0;
	unsigned int i;
	unsigned char *buf = malloc(WORKER_ALLOC_BYTES);

	if (buf != NULL) {
		for (i = 0; i < WORKER_ALLOC_BYTES; i++)
			buf[i] = (unsigned char)(i + acc);
	}
	for (i = 0; i < WORKER_LOOP_ITERS; i++)
		acc += i ^ (acc >> 1);

	/* Fold the buffer back in so the compiler can't elide the work. */
	if (buf != NULL) {
		acc += buf[acc % WORKER_ALLOC_BYTES];
		free(buf);
	}

	/* Keep acc observable; the exit code is the contract, not acc. */
	if (acc == 0xdeadbeef)
		_exit(WORKER_EXIT_CODE + 1); /* unreachable in practice */

	_exit(WORKER_EXIT_CODE);
}


/*
 * Spawn one worker via vfork()+execve(self, {self, "_worker", NULL}). Returns the
 * child pid (>0), or -1 with errno set (the caller classifies EAGAIN as LIMIT).
 * The child only execs/_exits — vfork-safe.
 */
static pid_t spawn_worker(char *self)
{
	char *argv[3];
	pid_t pid;

	argv[0] = self;
	argv[1] = "_worker";
	argv[2] = NULL;

	pid = vfork();
	if (pid < 0)
		return -1;
	if (pid == 0) {
		execve(self, argv, environ);
		_exit(127); /* exec failed — parent sees code 127, flagged below */
	}
	return pid;
}


/*
 * parallel mode: spawn up to N children concurrently, then waitpid-reap every
 * one and verify its exit code. fork->EAGAIN before reaching N is the process
 * cap (LIMIT); we record the max concurrent actually reached. A wrong/lost exit
 * code is a FAULT.
 */
static void run_parallel(sr_tally_t *t, char *self, int n)
{
	pid_t *pids = malloc((size_t)n * sizeof(pid_t));
	int spawned = 0, i, hit_cap = 0;
	int reaped = 0, bad_code = 0, lost = 0;
	long t0, t1;

	if (pids == NULL) {
		SR_FAULT(t, SUITE, "parallel", "cannot allocate pid table for n=%d", n);
		return;
	}

	SR_HB(SUITE, "parallel spawn start n=%d", n);
	t0 = ms_now();

	for (i = 0; i < n; i++) {
		pid_t p = spawn_worker(self);
		if (p < 0) {
			if (errno == EAGAIN) {
				/* The OS refused at the process cap — correct behaviour. */
				hit_cap = 1;
				break;
			}
			/* Any other spawn errno is not a legitimate limit. */
			SR_FAULT(t, SUITE, "parallel", "vfork/exec failed errno=%d (%s) at i=%d",
				errno, strerror(errno), i);
			break;
		}
		pids[spawned++] = p;
		if ((i & 7) == 0)
			SR_HB(SUITE, "parallel spawned=%d", spawned);
	}

	if (hit_cap) {
		SR_LIMIT(t, SUITE, "parallel", "fork EAGAIN at process cap; max concurrent=%d",
			spawned);
	}

	/* Reap all spawned children and verify each exit code. */
	SR_HB(SUITE, "parallel reap start spawned=%d", spawned);
	for (i = 0; i < spawned; i++) {
		int status = 0;
		pid_t w = waitpid(pids[i], &status, 0);
		if (w < 0) {
			if (errno == EINTR) {
				i--; /* retry this one */
				continue;
			}
			lost++;
			SR_FAULT(t, SUITE, "parallel", "waitpid(pid=%d) failed errno=%d (%s)",
				(int)pids[i], errno, strerror(errno));
			continue;
		}
		reaped++;
		if (!WIFEXITED(status) || WEXITSTATUS(status) != WORKER_EXIT_CODE) {
			bad_code++;
			if (WIFEXITED(status) && WEXITSTATUS(status) == 127)
				SR_FAULT(t, SUITE, "parallel", "child pid=%d failed to exec (code 127)",
					(int)pids[i]);
			else if (WIFSIGNALED(status))
				SR_FAULT(t, SUITE, "parallel", "child pid=%d killed by signal %d",
					(int)pids[i], WTERMSIG(status));
			else
				SR_FAULT(t, SUITE, "parallel", "child pid=%d wrong exit status=0x%x (want %d)",
					(int)pids[i], (unsigned)status, WORKER_EXIT_CODE);
		}
	}

	t1 = ms_now();
	SR_HB(SUITE, "parallel reap done reaped=%d", reaped);

	if (bad_code == 0 && lost == 0 && spawned > 0) {
		SR_OK(t, SUITE, "parallel", "spawned=%d reaped=%d all_exit=%d in %ld ms",
			spawned, reaped, WORKER_EXIT_CODE, t1 - t0);
	}
	else if (spawned == 0 && !hit_cap) {
		SR_FAULT(t, SUITE, "parallel", "spawned 0 children with no limit reported");
	}

	free(pids);
}


/*
 * churn mode: spawn+immediately reap one worker, ITERS times. A pid/handle leak
 * shows as a monotonic failure onset (spawns start failing with a non-limit
 * errno at some iteration) or a per-spawn slowdown. We track a baseline from the
 * early iterations and stop if either trips.
 */
static void run_churn(sr_tally_t *t, char *self, int iters)
{
	int i, ok = 0;
	long baseline_ms = 0;
	int slowdown_at = -1, fail_at = -1, limit_at = -1;
	int slow_run = 0; /* consecutive slow iterations */

	SR_HB(SUITE, "churn start iters=%d", iters);

	for (i = 0; i < iters; i++) {
		long a, b, delta;
		pid_t p;
		int status = 0;

		a = ms_now();
		p = spawn_worker(self);
		if (p < 0) {
			if (errno == EAGAIN) {
				limit_at = i;
				break; /* process cap during churn = LIMIT, not a leak fault */
			}
			fail_at = i;
			SR_FAULT(t, SUITE, "churn", "spawn failed at iter=%d errno=%d (%s)",
				i, errno, strerror(errno));
			break;
		}
		if (waitpid(p, &status, 0) < 0 && errno != EINTR) {
			fail_at = i;
			SR_FAULT(t, SUITE, "churn", "waitpid failed at iter=%d errno=%d (%s)",
				i, errno, strerror(errno));
			break;
		}
		if (!WIFEXITED(status) || WEXITSTATUS(status) != WORKER_EXIT_CODE) {
			fail_at = i;
			SR_FAULT(t, SUITE, "churn", "wrong exit at iter=%d status=0x%x (want %d)",
				i, (unsigned)status, WORKER_EXIT_CODE);
			break;
		}
		ok++;

		b = ms_now();
		delta = b - a;
		/* Establish a baseline once a handful of iterations have settled, then
		 * track a sustained run of slow spawns. A single slow iteration (e.g. an
		 * NFS hiccup on the worker's execve) resets the run, so only a monotonic
		 * leak-shaped slowdown is ever flagged. */
		if (i == 16) {
			baseline_ms = (delta > CHURN_SLOWDOWN_FLOOR_MS) ? delta : CHURN_SLOWDOWN_FLOOR_MS;
		}
		else if (baseline_ms > 0 && delta > CHURN_SLOWDOWN_FLOOR_MS
				&& delta > baseline_ms * CHURN_SLOWDOWN_FACTOR) {
			if (++slow_run >= CHURN_SLOWDOWN_RUN) {
				slowdown_at = i;
				break;
			}
		}
		else {
			slow_run = 0; /* fast iteration — not a sustained slowdown */
		}

		if ((i % 50) == 0)
			SR_HB(SUITE, "churn iter=%d ok=%d", i, ok);
	}

	SR_HB(SUITE, "churn end ok=%d", ok);

	if (limit_at >= 0) {
		SR_LIMIT(t, SUITE, "churn", "process cap reached at iter=%d (completed=%d)",
			limit_at, ok);
	}
	if (slowdown_at >= 0) {
		/* A sustained monotonic slowdown is the classic handle-leak signature. */
		SR_FAULT(t, SUITE, "churn", "sustained per-spawn slowdown >%dx baseline "
			"(%dms) for %d consecutive iters ending iter=%d (possible pid/handle leak)",
			CHURN_SLOWDOWN_FACTOR, (int)baseline_ms, CHURN_SLOWDOWN_RUN, slowdown_at);
	}
	if (fail_at < 0 && slowdown_at < 0) {
		SR_OK(t, SUITE, "churn", "completed=%d cycles, no leak onset", ok);
	}
}


int main(int argc, char *argv[])
{
	sr_tally_t tally = { 0 };
	const char *mode;
	char *self;
	long intensity;
	int do_parallel = 0, do_churn = 0;
	int n_parallel = DEFAULT_PARALLEL;
	int n_churn = DEFAULT_CHURN;

	sr_init();

	/* The internal worker path: re-exec'd copy of ourselves. Handle it before
	 * anything else and before printing any STRESS line. */
	if (argc >= 2 && strcmp(argv[1], "_worker") == 0)
		return run_worker();

	mode = (argc >= 2) ? argv[1] : "all";
	intensity = (argc >= 3) ? strtol(argv[2], NULL, 10) : 0;

	/* The re-exec target is argv[0]; it must be absolute (no PATH search). */
	self = argv[0];
	if (self == NULL || self[0] != '/') {
		SR_LIMIT(&tally, SUITE, "setup",
			"argv[0]=%s not absolute; cannot re-exec self for workers",
			self ? self : "(null)");
		SR_SUMMARY(&tally, SUITE, "mode=%s", mode);
		return 0;
	}

	if (strcmp(mode, "parallel") == 0) {
		do_parallel = 1;
		if (intensity > 0)
			n_parallel = (int)intensity;
	}
	else if (strcmp(mode, "churn") == 0) {
		do_churn = 1;
		if (intensity > 0)
			n_churn = (int)intensity;
	}
	else { /* "all" */
		do_parallel = do_churn = 1;
	}

	if (do_parallel)
		run_parallel(&tally, self, n_parallel);
	if (do_churn)
		run_churn(&tally, self, n_churn);

	SR_SUMMARY(&tally, SUITE, "mode=%s self=%s", mode, self);
	return 0;
}
