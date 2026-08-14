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
/* (2) wide-char funcs libphoenix's <wchar.h> lacks (declare so compile proceeds;
 *     the link pass reveals which need real definitions in libphoenix). */
long               wcstol(const wchar_t *__restrict, wchar_t **__restrict, int);
unsigned long      wcstoul(const wchar_t *__restrict, wchar_t **__restrict, int);
long long          wcstoll(const wchar_t *__restrict, wchar_t **__restrict, int);
unsigned long long wcstoull(const wchar_t *__restrict, wchar_t **__restrict, int);
double             wcstod(const wchar_t *__restrict, wchar_t **__restrict);
float              wcstof(const wchar_t *__restrict, wchar_t **__restrict);
long double        wcstold(const wchar_t *__restrict, wchar_t **__restrict);
wchar_t           *wcstok(wchar_t *__restrict, const wchar_t *__restrict, wchar_t **__restrict);
wchar_t           *wcsstr(const wchar_t *, const wchar_t *);
size_t             wcsspn(const wchar_t *, const wchar_t *);
size_t             wcscspn(const wchar_t *, const wchar_t *);
wchar_t           *wcspbrk(const wchar_t *, const wchar_t *);
/* (3) clock_getres: Phoenix has clock_gettime but not clock_getres; CPython uses
 *     it only to report clock resolution (time.get_clock_info). Nominal 1ns. */
static inline int clock_getres(clockid_t __id, struct timespec *__res) {
    (void)__id; if (__res) { __res->tv_sec = 0; __res->tv_nsec = 1; } return 0;
}
/* (4) O_NOFOLLOW: Phoenix fcntl.h lacks it; define 0 (no nofollow enforcement). */
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif
#endif
