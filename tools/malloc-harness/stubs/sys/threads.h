/* Host stub for libphoenix <sys/threads.h>: the harness is single-threaded, so
 * the allocator lock is a no-op.  Kept as real functions (not empty macros) so
 * the call sites still compile exactly as written. */
#ifndef HZ_SYS_THREADS_H
#define HZ_SYS_THREADS_H

#include <stdint.h>

typedef int handle_t;

static inline int mutexCreate(handle_t *h)
{
	*h = 1;
	return 0;
}

static inline int mutexLock(handle_t h)
{
	(void)h;
	return 0;
}

static inline int mutexUnlock(handle_t h)
{
	(void)h;
	return 0;
}

#endif
