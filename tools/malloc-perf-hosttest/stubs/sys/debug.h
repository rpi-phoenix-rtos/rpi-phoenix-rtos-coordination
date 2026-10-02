/* Host stub for libphoenix <sys/debug.h>. */
#ifndef MPH_SYS_DEBUG_H
#define MPH_SYS_DEBUG_H

#include <stdio.h>

static inline void debug(const char *s)
{
	fputs(s, stderr);
}

#endif
