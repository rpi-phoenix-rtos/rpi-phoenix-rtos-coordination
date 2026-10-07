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
