/*
 * Phoenix-RTOS X11 port — startup milestone markers.
 *
 * Compiled in only with -DPHX_DIAG. The Pi 4 netboot bring-up stalls in
 * StartUp()/wScreenInit() after the defaults load with no further output and
 * no crash; PHX_MARK() routes through __wmessage (the UART-proven wwarning
 * path) with an explicit flush, so the LAST line printed pins the stall point.
 * Compiled to nothing for upstream/normal builds.
 *
 * Author: Witold Bołt
 */
#ifndef PHX_DIAG_H
#define PHX_DIAG_H

/*
 * wmessage() is provided wherever this header is used: in src/*.c via
 * WindowMaker.h and in WINGs/*.c via WINGsP.h -> WINGs/WINGs.h -> WUtil.h.
 * We deliberately do NOT #include "WUtil.h" here so the header works unmodified
 * from both the wmaker src/ and the WINGs/ trees (different include roots).
 */
#ifdef PHX_DIAG
#include <stdio.h>
#define PHX_MARK(...) do { wmessage("PHX_DIAG: " __VA_ARGS__); fflush(stderr); fflush(stdout); } while (0)
#else
#define PHX_MARK(...) do { } while (0)
#endif

#endif /* PHX_DIAG_H */
