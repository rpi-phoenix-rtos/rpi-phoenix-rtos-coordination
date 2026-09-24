/* Host shim: the three Phoenix-specific things time.c needs, defined exactly as
 * the target does. Only time()/clock_gettime() use gettime(); the calendar
 * functions under test do not touch it. */
#include <errno.h>
#include <time.h>

#define EOK 0

static inline int SET_ERRNO(int x)
{
	if (x < 0) {
		errno = -x;
		return -1;
	}
	return x;
}

int gettime(time_t *raw, time_t *offs);
int settime(time_t t);
