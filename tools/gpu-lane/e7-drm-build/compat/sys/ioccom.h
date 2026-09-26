/* E7 compat: libdrm's non-Linux drm.h branch includes <sys/ioccom.h> (BSD).
 * Phoenix's _IO/_IOR/_IOW/_IOWR live in <sys/ioctl.h> and use the BSD layout
 * (IOCPARM_MASK 0x1fff, IOC_IN/OUT = 0x8/0x4 << 28) -- except IOC_VOID is 0 and
 * 0x20000000 means IOC_NESTED. */
#include <sys/ioctl.h>
