/* Host stub for libphoenix <sys/debug.h>. */
#ifndef HZ_SYS_DEBUG_H
#define HZ_SYS_DEBUG_H

#include <stdio.h>

static inline void debug(const char *s)
{
	fputs(s, stderr);
	fflush(stderr);
}

#endif
