/* E7 compat: prototypes for POSIX/GNU calls libphoenix does not provide yet.
 * Implementations (stubs) live in compat/e7-compat.c. Study-only. */
#ifndef PHOENIX_E7_COMPAT_H
#define PHOENIX_E7_COMPAT_H
#include <stdio.h>
#include <stddef.h>
FILE *open_memstream(char **bufp, size_t *sizep);   /* POSIX.1-2008; libphoenix: absent */
#endif
