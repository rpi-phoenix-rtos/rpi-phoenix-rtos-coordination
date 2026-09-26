/* E7: simulates the libphoenix fix -- open_memstream (POSIX.1-2008). */
#include_next <stdio.h>
#ifndef E7_OPEN_MEMSTREAM
#define E7_OPEN_MEMSTREAM
#ifdef __cplusplus
extern "C"
#endif
FILE *open_memstream(char **bufp, size_t *sizep);
#endif
