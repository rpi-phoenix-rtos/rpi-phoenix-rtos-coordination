/* E7 study: force-included into Mesa TUs. Every item is a libphoenix GAP that the
 * real M3 port should close in libphoenix itself (owner rule: implement missing libc),
 * not carry here. Stubs live in compat/e7-compat.c. */
#ifndef PHOENIX_E7_MESA_COMPAT_H
#define PHOENIX_E7_MESA_COMPAT_H
#include <stdio.h>
#include <stddef.h>
#include <assert.h>
#include <inttypes.h>
#include <pthread.h>
#ifndef __cplusplus
# ifndef static_assert
#  define static_assert _Static_assert   /* C11 7.2: <assert.h> must define it; libphoenix does not */
# endif
#endif
#ifndef SCNxPTR
# define SCNxPTR "lx"                     /* C99 7.8.1; missing from libphoenix <inttypes.h> */
# define SCNuPTR "lu"
#endif
#ifdef __cplusplus
extern "C" {
#endif
FILE *open_memstream(char **bufp, size_t *sizep);            /* POSIX.1-2008 */
#ifndef PTHREAD_CANCEL_ASYNCHRONOUS
# define PTHREAD_CANCEL_DEFERRED     0
# define PTHREAD_CANCEL_ASYNCHRONOUS 1
#endif
int pthread_setcanceltype(int type, int *oldtype);          /* POSIX; libphoenix has only setcancelstate */
#ifdef __cplusplus
}
#endif
#endif
