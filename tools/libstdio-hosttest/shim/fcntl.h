/* libphoenix stdio tests the access mode as BITS (O_RDONLY is 1, not 0 as on
 * Linux), e.g. `mode & O_RDONLY` to refuse writes. Give it Phoenix's values;
 * support.c translates them back before calling the host open(). */
#ifndef SH_FCNTL_H
#define SH_FCNTL_H
#include_next <fcntl.h>
#undef O_RDONLY
#undef O_WRONLY
#undef O_RDWR
#undef O_ACCMODE
#define O_RDONLY  0x1
#define O_WRONLY  0x2
#define O_RDWR    0x4
#define O_ACCMODE 0x7
#endif
