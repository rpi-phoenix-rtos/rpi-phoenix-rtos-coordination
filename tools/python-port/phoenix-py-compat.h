/* Phoenix CPython bring-up shim (-include'd first in every TU).
 * (1) Pull in system types early so CPython internal headers see complete
 *     struct timeval / struct rusage regardless of their own include order. */
#ifndef PHOENIX_PY_COMPAT_H
#define PHOENIX_PY_COMPAT_H
#include <fcntl.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <time.h>
#include <wchar.h>
#include <stddef.h>
/* (2) wide-char funcs (wcstol/wcstok/wcsstr/...) are now IN libphoenix <wchar.h>. */
/* (3) clock_getres: Phoenix has clock_gettime but not clock_getres; CPython uses
 *     it only to report clock resolution (time.get_clock_info). Nominal 1ns. */
static inline int clock_getres(clockid_t __id, struct timespec *__res) {
    (void)__id; if (__res) { __res->tv_sec = 0; __res->tv_nsec = 1; } return 0;
}
/* (4) O_NOFOLLOW: Phoenix fcntl.h lacks it; define 0 (no nofollow enforcement). */
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif
#include <unistd.h>
/* (5) sysconf names CPython calls directly that Phoenix's <unistd.h> lacks.
 *     Unknown names -> Phoenix sysconf returns -1, which CPython tolerates;
 *     _SC_PAGE_SIZE aliases the real _SC_PAGESIZE so mmap page size is correct. */
#ifndef _SC_PAGE_SIZE
#define _SC_PAGE_SIZE _SC_PAGESIZE
#endif
#ifndef _SC_NPROCESSORS_ONLN
#define _SC_NPROCESSORS_ONLN 1001
#endif
#ifndef _SC_TTY_NAME_MAX
#define _SC_TTY_NAME_MAX 1002
#endif
#ifndef _SC_SEM_VALUE_MAX
#define _SC_SEM_VALUE_MAX 1003
#endif
#ifndef _SC_GETGR_R_SIZE_MAX
#define _SC_GETGR_R_SIZE_MAX 1004
#endif
#ifndef _SC_GETPW_R_SIZE_MAX
#define _SC_GETPW_R_SIZE_MAX 1005
#endif
#endif
