/*
 * The Phoenix system calls libphoenix's pthread.c makes, on top of host
 * (glibc) threads. Each shim_X stands in for X: the Makefile renames every
 * call pthread.c makes outside the host C library to shim_X, and stubs.c
 * (generated) aborts on any of them not implemented here -- so a code path
 * this harness does not model fails loudly instead of silently.
 *
 * Phoenix ABI as seen by pthread.c: handle_t and tids are int, and a thread
 * ends with endthread(). The thread a Phoenix thread "runs on" is a host
 * thread; the stack pthread.c mmaps for it is left unused.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
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
	pthread_mutex_init(&shim_mutexes[idx], NULL);
	*h = idx;
	return 0;
}


int shim_mutexLock(int h)
{
	return -pthread_mutex_lock(&shim_mutexes[h]);
}


int shim_mutexUnlock(int h)
{
	return -pthread_mutex_unlock(&shim_mutexes[h]);
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
	/* SIGCANCEL is a kernel-side kill on Phoenix: model it as an asynchronous
	 * host cancel (see shim_sys_tkill) */
	(void)pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	s.start(s.arg);
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
	if (pthread_create(&t, NULL, shim_trampoline, s) != 0) {
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
	return -pthread_cancel(shim_threadOf(tid));
}


void *shim_mmap(void *addr, size_t len, int prot, int flags, int fd, long off)
{
	(void)addr;
	(void)prot;
	(void)flags;
	(void)fd;
	(void)off;
	/* Thread stacks only; Phoenix's flag values differ from Linux's */
	return mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
}


int shim_munmap(void *addr, size_t len)
{
	return munmap(addr, len);
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
