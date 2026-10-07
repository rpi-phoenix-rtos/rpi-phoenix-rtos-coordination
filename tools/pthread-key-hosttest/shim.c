/*
 * The Phoenix system calls libphoenix's pthread.c makes, on top of host
 * (glibc) threads. Each shim_X stands in for X: the Makefile renames every
 * call pthread.c makes outside the host C library to shim_X, and stubs.c
 * (generated) aborts on any of them not implemented here -- so a code path
 * this harness does not model fails loudly instead of silently.
 *
 * Phoenix ABI as seen by pthread.c: handle_t and tids are int, and a thread
 * ends with endthread(). The thread a Phoenix thread "runs on" is a host
 * thread; the stack pthread.c mmaps for it is left unused, but counted, so
 * that main.c can tell whether every one was unmapped again.
 *
 * The kernel lets any thread release a plain lock, and reports a release of
 * one nobody holds only in a DEBUG_THREADS build. Both are bugs in pthread.c,
 * so the shim's locks check their owner and abort on either.
 *
 * SIGCANCEL makes the kernel end a thread wherever it is, and release the
 * locks it held (thread_destroy()). A host pthread_cancel() cannot stand in
 * for it: delivered inside ThreadSanitizer's runtime, its unwinding deadlocks
 * there. So the shim kills with a signal of its own, whose handler jumps back
 * to the thread's start (shim_trampoline()), and releases the locks itself.
 * Under ThreadSanitizer the handler runs at a point the runtime chooses.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define SHIM_MAX 65536

static pthread_mutex_t shim_mutexes[SHIM_MAX];
static int shim_mutexCount;
static pthread_t shim_threads[SHIM_MAX];
static int shim_tidNext = 1;
static __thread int shim_tid;
static pthread_mutex_t shim_lock = PTHREAD_MUTEX_INITIALIZER;
static int shim_stacks; /* mapped by pthread.c and not unmapped yet */

#define SHIM_HELD_MAX 16
static __thread int shim_held[SHIM_HELD_MAX]; /* locks this thread may hold */
static __thread int shim_heldCount;

#define SHIM_SIGKILL SIGUSR2 /* SIGCANCEL, see above */
static __thread sigjmp_buf shim_killJmp;
static __thread int shim_killable;


static int shim_newTid(void)
{
	int tid = __atomic_fetch_add(&shim_tidNext, 1, __ATOMIC_RELAXED);
	if (tid >= SHIM_MAX) {
		fprintf(stderr, "shim: out of tids\n");
		abort();
	}
	return tid;
}


int shim_gettid(void)
{
	if (shim_tid == 0) {
		shim_tid = shim_newTid(); /* the main thread, or one this shim did not start */
		__atomic_store_n(&shim_threads[shim_tid], pthread_self(), __ATOMIC_RELEASE);
	}
	return shim_tid;
}


int shim_getpid(void)
{
	return getpid();
}


int shim_getPriority(void)
{
	return 4;
}


int shim_schedInfo(int policy, void *info)
{
	(void)policy;
	(void)info; /* only cached by pthread.c, never needed here */
	return 0;
}


int shim_mutexCreate(int *h)
{
	pthread_mutex_lock(&shim_lock);
	int idx = shim_mutexCount++;
	pthread_mutex_unlock(&shim_lock);
	if (idx >= SHIM_MAX) {
		abort();
	}
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
	pthread_mutex_init(&shim_mutexes[idx], &attr);
	pthread_mutexattr_destroy(&attr);
	*h = idx;
	return 0;
}


static void shim_killMask(int how);


/* The kill signal is held off while a lock is taken or released, so that the
 * record of the locks this thread holds is exact when the kill lands */
int shim_mutexLock(int h)
{
	shim_killMask(SIG_BLOCK);
	int err = pthread_mutex_lock(&shim_mutexes[h]);
	if (err == EDEADLK) {
		fprintf(stderr, "shim: mutexLock(%d) of a lock this thread already holds\n", h);
		abort();
	}
	if (err == 0) {
		if (shim_heldCount == SHIM_HELD_MAX) {
			abort();
		}
		shim_held[shim_heldCount++] = h;
	}
	shim_killMask(SIG_UNBLOCK);
	return -err;
}


int shim_mutexUnlock(int h)
{
	shim_killMask(SIG_BLOCK);
	int err = pthread_mutex_unlock(&shim_mutexes[h]);
	if (err == EPERM) {
		fprintf(stderr, "shim: mutexUnlock(%d) of a lock this thread does not hold\n", h);
		abort();
	}
	for (int i = shim_heldCount - 1; i >= 0; i--) {
		if (shim_held[i] == h) {
			shim_held[i] = shim_held[--shim_heldCount];
			break;
		}
	}
	shim_killMask(SIG_UNBLOCK);
	return -err;
}


/* A killed thread's locks, released as the kernel does when a thread dies */
static void shim_releaseHeld(void)
{
	while (shim_heldCount > 0) {
		(void)pthread_mutex_unlock(&shim_mutexes[shim_held[--shim_heldCount]]);
	}
}


static void shim_onKill(int sig)
{
	(void)sig;
	if (shim_killable == 0) {
		fprintf(stderr, "shim: kill of a thread the shim did not start\n");
		abort();
	}
	siglongjmp(shim_killJmp, 1);
}


static void shim_killInit(void)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = shim_onKill;
	sigemptyset(&sa.sa_mask);
	(void)sigaction(SHIM_SIGKILL, &sa, NULL);
}


static void shim_killMask(int how)
{
	sigset_t set;

	sigemptyset(&set);
	sigaddset(&set, SHIM_SIGKILL);
	(void)pthread_sigmask(how, &set, NULL);
}


struct shim_start {
	void (*start)(void *);
	void *arg;
	int tid;
};


static void *shim_trampoline(void *p)
{
	struct shim_start s = *(struct shim_start *)p;

	free(p);
	shim_tid = s.tid;
	/* Started with the kill signal blocked (shim_beginthreadex()), so that one
	 * sent early waits for this */
	if (sigsetjmp(shim_killJmp, 0) == 0) {
		shim_killable = 1;
		shim_killMask(SIG_UNBLOCK);
		s.start(s.arg); /* ends in endthread(), never returns */
	}
	/* Killed */
	shim_killMask(SIG_BLOCK);
	shim_releaseHeld();
	return NULL;
}


int shim_beginthreadex(void (*start)(void *), int priority, void *stack, unsigned int stacksz, void *arg, int *id)
{
	struct shim_start *s = malloc(sizeof(*s));
	pthread_t t;
	int tid;

	(void)priority;
	(void)stack;
	(void)stacksz;
	if (s == NULL) {
		return -ENOMEM;
	}
	s->start = start;
	s->arg = arg;
	s->tid = tid = shim_newTid();
	if (id != NULL) {
		*id = tid;
	}
	static pthread_once_t once = PTHREAD_ONCE_INIT;
	(void)pthread_once(&once, shim_killInit);
	sigset_t set, old;
	sigemptyset(&set);
	sigaddset(&set, SHIM_SIGKILL);
	(void)pthread_sigmask(SIG_BLOCK, &set, &old);
	int err = pthread_create(&t, NULL, shim_trampoline, s);
	(void)pthread_sigmask(SIG_SETMASK, &old, NULL);
	if (err != 0) {
		free(s);
		return -EAGAIN;
	}
	__atomic_store_n(&shim_threads[tid], t, __ATOMIC_RELEASE);
	return 0;
}


/* The host thread record may be stored after the thread already runs (and
 * even exits): wait for it */
static pthread_t shim_threadOf(int tid)
{
	pthread_t t;

	while ((t = __atomic_load_n(&shim_threads[tid], __ATOMIC_ACQUIRE)) == 0) {
		sched_yield();
	}
	return t;
}


__attribute__((noreturn)) void shim_endthread(void)
{
	/* A SIGCANCEL that reaches a thread already in endthread() changes nothing */
	shim_killMask(SIG_BLOCK);
	pthread_exit(NULL);
}


int shim_threadJoin(int tid, long timeout)
{
	(void)timeout;
	pthread_t t = shim_threadOf(tid);
	int err = pthread_join(t, NULL);
	return (err == 0) ? tid : -err;
}


int shim_sys_tkill(int pid, int tid, int sig)
{
	(void)pid;
	if (sig != 32) { /* SIGCANCEL */
		fprintf(stderr, "shim: tkill(%d) not modelled\n", sig);
		abort();
	}
	return -pthread_kill(shim_threadOf(tid), SHIM_SIGKILL);
}


void *shim_mmap(void *addr, size_t len, int prot, int flags, int fd, long off)
{
	(void)addr;
	(void)prot;
	(void)flags;
	(void)fd;
	(void)off;
	/* Thread stacks only; Phoenix's flag values differ from Linux's */
	void *p = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p != MAP_FAILED) {
		__atomic_fetch_add(&shim_stacks, 1, __ATOMIC_RELAXED);
	}
	return p;
}


int shim_munmap(void *addr, size_t len)
{
	int err = munmap(addr, len);
	if (err == 0) {
		__atomic_fetch_sub(&shim_stacks, 1, __ATOMIC_RELAXED);
	}
	return err;
}


/* For main.c, not pthread.c */
int shim_stacksMapped(void)
{
	return __atomic_load_n(&shim_stacks, __ATOMIC_RELAXED);
}


int shim_mprotect(void *addr, size_t len, int prot)
{
	(void)addr;
	(void)len;
	(void)prot;
	return 0;
}


void shim__errno_new(void *e)
{
	(void)e;
}


void shim__errno_remove(void *e)
{
	(void)e;
}


int shim_fegetenv(void *env)
{
	(void)env;
	return 0;
}


int shim_fesetenv(const void *env)
{
	(void)env;
	return 0;
}


int shim_sigfillset(void *set)
{
	(void)set;
	return 0;
}


int shim_sigprocmask(int how, const void *set, void *old)
{
	(void)how;
	(void)set;
	(void)old;
	return 0;
}


int shim_usleep(unsigned int us)
{
	return usleep(us);
}


/*
 * Clocks and futexes, for condition variables and mutexes (sys/ulock.c).
 *
 * Phoenix keeps one clock: gettime() reports the monotonic time (here the
 * host's CLOCK_MONOTONIC) and an offset the wall clock adds to it, which
 * settime() changes. The shim keeps that offset itself, so a test can step
 * the wall clock -- as the board does when it first sets its time -- without
 * touching the host's. Phoenix's clock ids differ from Linux's; the suites
 * that use these see Phoenix's (rename-cond.h).
 *
 * futexWait() follows the kernel's (proc/futex.c, proc_clockTimeoutToAbsTime()):
 * a CLOCK_REALTIME deadline is translated to a monotonic one when the call is
 * made, a passed deadline is -ETIME at once, and the word is compared under
 * the same lock futexWake() takes. With SHIM_SPURIOUS set in the environment,
 * a sleeper also returns EOK, as from a wake-up meant for someone else, every
 * SHIM_SPURIOUS_US: the paths that run again after such a wake-up are rare on
 * the board, and this exercises them all the time.
 */

#define SHIM_PH_CLOCK_MONOTONIC     0 /* time.h clockid_t values */
#define SHIM_PH_CLOCK_MONOTONIC_RAW 1
#define SHIM_PH_CLOCK_REALTIME      2

#define SHIM_PH_TIMEOUT_RELATIVE  0 /* kernel timeout clocks (PH_CLOCK_*) */
#define SHIM_PH_TIMEOUT_REALTIME  1
#define SHIM_PH_TIMEOUT_MONOTONIC 2

#define SHIM_SPURIOUS_US 20000LL

static long long shim_utcOffs;


static long long shim_rawUs(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ((long long)ts.tv_sec * 1000000LL) + (ts.tv_nsec / 1000);
}


static void shim_clockInit(void)
{
	struct timespec ts;

	/* Starts as the host's wall clock */
	clock_gettime(CLOCK_REALTIME, &ts);
	__atomic_store_n(&shim_utcOffs, ((long long)ts.tv_sec * 1000000LL) + (ts.tv_nsec / 1000) - shim_rawUs(), __ATOMIC_RELAXED);
}


static pthread_once_t shim_clockOnce = PTHREAD_ONCE_INIT;


int shim_gettime(long long *raw, long long *offs)
{
	(void)pthread_once(&shim_clockOnce, shim_clockInit);
	if (raw != NULL) {
		*raw = shim_rawUs();
	}
	if (offs != NULL) {
		*offs = __atomic_load_n(&shim_utcOffs, __ATOMIC_RELAXED);
	}
	return 0;
}


/* clock_gettime() and clock_settime() as the test sees them (Phoenix clock ids) */
int shim_clockGettime(int clock, struct timespec *ts)
{
	long long raw, offs;

	(void)shim_gettime(&raw, &offs);
	switch (clock) {
		case SHIM_PH_CLOCK_REALTIME:
			raw += offs;
			break;
		case SHIM_PH_CLOCK_MONOTONIC:
		case SHIM_PH_CLOCK_MONOTONIC_RAW:
			break;
		default:
			errno = EINVAL;
			return -1;
	}
	ts->tv_sec = raw / 1000000LL;
	ts->tv_nsec = (raw % 1000000LL) * 1000;
	return 0;
}


int shim_clockSettime(int clock, const struct timespec *ts)
{
	long long raw;

	if ((clock != SHIM_PH_CLOCK_REALTIME) || (ts->tv_sec < 0) || (ts->tv_nsec < 0) || (ts->tv_nsec >= 1000000000L)) {
		errno = EINVAL;
		return -1;
	}
	(void)shim_gettime(&raw, NULL);
	__atomic_store_n(&shim_utcOffs, ((long long)ts->tv_sec * 1000000LL) + (ts->tv_nsec / 1000) - raw, __ATOMIC_RELAXED);
	return 0;
}


struct shim_futexWaiter {
	volatile unsigned int *addr;
	int woken;
	struct shim_futexWaiter *next;
};

static pthread_mutex_t shim_futexLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t shim_futexCond;
static struct shim_futexWaiter *shim_futexWaiters;
static int shim_spurious;
static pthread_once_t shim_futexOnce = PTHREAD_ONCE_INIT;


static void shim_futexInit(void)
{
	pthread_condattr_t attr;
	const char *spurious;

	pthread_condattr_init(&attr);
	pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
	pthread_cond_init(&shim_futexCond, &attr);
	pthread_condattr_destroy(&attr);
	spurious = getenv("SHIM_SPURIOUS");
	shim_spurious = (spurious != NULL) && (spurious[0] != '\0') && (spurious[0] != '0');
}


int shim_futexWait(volatile unsigned int *addr, unsigned int val, long long timeout, int clock)
{
	struct shim_futexWaiter w, **pw;
	long long now, offs, deadline = 0, until;
	struct timespec ts;
	sigset_t ks, old;
	int err = 0;

	(void)pthread_once(&shim_futexOnce, shim_futexInit);
	(void)shim_gettime(&now, &offs);

	if (timeout < 0) {
		return -EINVAL;
	}
	if (timeout != 0) {
		switch (clock) {
			case SHIM_PH_TIMEOUT_REALTIME:
				if (now + offs > timeout) {
					return -ETIME;
				}
				deadline = timeout - offs;
				break;
			case SHIM_PH_TIMEOUT_MONOTONIC:
				if (now > timeout) {
					return -ETIME;
				}
				deadline = timeout;
				break;
			case SHIM_PH_TIMEOUT_RELATIVE:
				deadline = now + timeout;
				break;
			default:
				return -EINVAL;
		}
	}

	/* A kill waits until the sleep ends: jumping out of the host's condition
	 * variable wait would leave this waiter on the list */
	sigemptyset(&ks);
	sigaddset(&ks, SHIM_SIGKILL);
	(void)pthread_sigmask(SIG_BLOCK, &ks, &old);

	pthread_mutex_lock(&shim_futexLock);
	if (__atomic_load_n(addr, __ATOMIC_ACQUIRE) != val) {
		pthread_mutex_unlock(&shim_futexLock);
		(void)pthread_sigmask(SIG_SETMASK, &old, NULL);
		return -EAGAIN;
	}
	w.addr = addr;
	w.woken = 0;
	w.next = shim_futexWaiters;
	shim_futexWaiters = &w;

	for (;;) {
		if (w.woken != 0) {
			break;
		}
		now = shim_rawUs();
		if ((deadline != 0) && (now >= deadline)) {
			err = -ETIME;
			break;
		}
		until = deadline;
		if (shim_spurious != 0) {
			if ((until == 0) || (now + SHIM_SPURIOUS_US < until)) {
				until = now + SHIM_SPURIOUS_US;
			}
		}
		if (until == 0) {
			pthread_cond_wait(&shim_futexCond, &shim_futexLock);
		}
		else {
			ts.tv_sec = until / 1000000LL;
			ts.tv_nsec = (until % 1000000LL) * 1000;
			if ((pthread_cond_timedwait(&shim_futexCond, &shim_futexLock, &ts) == ETIMEDOUT) &&
					(until != deadline) && (w.woken == 0)) {
				break; /* a wake-up for someone else: EOK, the word unchanged */
			}
		}
	}

	for (pw = &shim_futexWaiters; *pw != &w; pw = &(*pw)->next) {
	}
	*pw = w.next;
	pthread_mutex_unlock(&shim_futexLock);
	(void)pthread_sigmask(SIG_SETMASK, &old, NULL);

	return (w.woken != 0) ? 0 : err;
}


int shim_futexWake(volatile unsigned int *addr, unsigned int count)
{
	struct shim_futexWaiter *w;
	int n = 0;

	(void)pthread_once(&shim_futexOnce, shim_futexInit);
	pthread_mutex_lock(&shim_futexLock);
	for (w = shim_futexWaiters; (w != NULL) && ((unsigned int)n < count); w = w->next) {
		if ((w->addr == addr) && (w->woken == 0)) {
			w->woken = 1;
			n++;
		}
	}
	if (n != 0) {
		pthread_cond_broadcast(&shim_futexCond);
	}
	pthread_mutex_unlock(&shim_futexLock);
	return n;
}
