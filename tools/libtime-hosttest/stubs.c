#include <stdio.h>
#include <stdlib.h>
#include "shim/sys/threads.h"

int threadinfo(int tid, unsigned int flags, threadinfo_t *info) { (void)tid; (void)flags; (void)info; abort(); }

/* One-mutex model: the time zone lock. Held twice = a deadlock on the target. */
static int mutex_created, mutex_held;

int mutexCreate(handle_t *h)
{
	*h = 1;
	mutex_created = 1;
	return 0;
}

int mutexLock(handle_t h)
{
	if (h != 1 || !mutex_created || mutex_held) {
		fprintf(stderr, "STUB mutexLock(%d): %s\n", h, mutex_held ? "RECURSIVE (target would deadlock)" : "not created");
		abort();
	}
	mutex_held = 1;
	return 0;
}

int mutexUnlock(handle_t h)
{
	if (h != 1 || !mutex_held) {
		fprintf(stderr, "STUB mutexUnlock(%d): not held\n", h);
		abort();
	}
	mutex_held = 0;
	return 0;
}

/*
 * Stubs for the three Phoenix syscalls time.c references. None of them is on a
 * path this harness exercises -- only time(), clock_gettime/settime() and
 * nanosleep() call them, and the calendar functions under test do not. They
 * exist so the object links; if one is ever reached the abort() makes that
 * loud rather than silently returning a plausible zero.
 */
#include <time.h>
int gettime(time_t *raw, time_t *offs);
int settime(time_t t);
int nsleep(time_t *sec, long *nsec, int clockid, int flags);

int gettime(time_t *raw, time_t *offs)
{
	(void)raw;
	(void)offs;
	abort();
}

int settime(time_t t)
{
	(void)t;
	abort();
}

int nsleep(time_t *sec, long *nsec, int clockid, int flags)
{
	(void)sec;
	(void)nsec;
	(void)clockid;
	(void)flags;
	abort();
}

#include "shim/sys/proc.h"

int sys_cpuTime(pid_t pid, int tid, time_t *cpuTime, cpuTimes_t *cpuTimes)
{
	(void)pid;
	(void)tid;
	(void)cpuTime;
	(void)cpuTimes;
	abort();
}

int pidExists(pid_t pid)
{
	(void)pid;
	abort();
}
