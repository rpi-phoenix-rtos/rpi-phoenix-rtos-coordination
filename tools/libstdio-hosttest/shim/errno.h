#ifndef SH_ERRNO_H
#define SH_ERRNO_H
#include_next <errno.h>
static inline int set_errno(int x)
{
	if (x < 0) {
		errno = -x;
		return -1;
	}
	return x;
}
#define SET_ERRNO set_errno
#endif
