#ifndef SH_SYS_DEBUG_H
#define SH_SYS_DEBUG_H
#include <string.h>
#include <unistd.h>
static inline void debug(const char *s)
{
	(void)write(2, s, strlen(s));
}
#endif
