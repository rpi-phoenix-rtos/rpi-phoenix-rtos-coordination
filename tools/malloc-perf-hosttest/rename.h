/* Force-included into a libphoenix test built for the host: route its
 * allocator calls to the phx_ copy of malloc_dl.c (alloc.c). The host headers
 * the tests use come first, so the renames below cannot touch their
 * declarations. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "malloc.h"

void *phx_malloc(size_t size);
void *phx_calloc(size_t n, size_t size);
void *phx_realloc(void *ptr, size_t size);
void phx_free(void *ptr);
void phx_mallocInfo(mallocInfo_t *info);
int phx_malloc_trim(size_t pad);

#define malloc      phx_malloc
#define calloc      phx_calloc
#define realloc     phx_realloc
#define free        phx_free
#define mallocInfo  phx_mallocInfo
#define malloc_trim phx_malloc_trim
