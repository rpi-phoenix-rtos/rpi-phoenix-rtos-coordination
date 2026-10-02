/* Host stub for libphoenix <sys/threads.h>: just what sys/ulock.c and
 * sys/ulock-internal.h need, with futexWait()/futexWake() on the Linux futex. */
#ifndef MPH_SYS_THREADS_H
#define MPH_SYS_THREADS_H

#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/futex.h>

#ifndef EOK
#define EOK 0
#endif

#define PH_CLOCK_RELATIVE 0

static inline int futexWait(volatile unsigned int *addr, unsigned int value, time_t timeout, int clock)
{
	(void)timeout;
	(void)clock;
	if (syscall(SYS_futex, addr, FUTEX_WAIT_PRIVATE, value, NULL, NULL, 0) != 0) {
		return -errno;
	}
	return EOK;
}

static inline int futexWake(volatile unsigned int *addr, unsigned int n)
{
	return (int)syscall(SYS_futex, addr, FUTEX_WAKE_PRIVATE, n, NULL, NULL, 0);
}

#endif
