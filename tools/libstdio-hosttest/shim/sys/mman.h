/* Phoenix's MAP_ANONYMOUS stands alone (MAP_PRIVATE is 0 there); Linux needs
 * MAP_PRIVATE with it or mmap() fails with EINVAL. */
#ifndef SH_SYS_MMAN_H
#define SH_SYS_MMAN_H
#include_next <sys/mman.h>
#undef MAP_ANONYMOUS
#define MAP_ANONYMOUS (0x20 | MAP_PRIVATE)
#endif
