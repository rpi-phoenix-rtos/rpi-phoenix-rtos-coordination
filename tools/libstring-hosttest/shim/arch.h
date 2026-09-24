/* host shim: libphoenix arch.h is target-only; string.c needs only this macro */
#define __EXPORT_INLINE __inline __attribute__((__gnu_inline__))
