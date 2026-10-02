/* Host stub for libphoenix <malloc.h> (the host's has no mallocInfo_t). */
#ifndef MPH_MALLOC_H
#define MPH_MALLOC_H

#include <stddef.h>

typedef struct _mallocInfo_t {
	size_t mapsz;
	size_t freesz;
	size_t maxalloc;
} mallocInfo_t;

#endif
