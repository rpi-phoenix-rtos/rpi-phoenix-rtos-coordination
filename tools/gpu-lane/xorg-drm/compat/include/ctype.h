/*
 * xorg-drm compat (libphoenix defect): libphoenix's <ctype.h> defines isdigit()
 * and friends as macros that evaluate their argument MORE THAN ONCE, e.g.
 *     #define __isdigit(c) (((c) >= '0' && (c) <= '9') ? 1 : 0)
 * C17 7.1.4 requires a library macro to evaluate each argument exactly once.
 * The X server's config scanner reads numbers with
 *     while (isdigit(c = configBuf[configPos++]) || ...)
 * (hw/xfree86/parser/scan.c:373), which then advanced two characters per test:
 * "DefaultDepth 24" parsed as 2 and Xorg-drm stopped with "Given depth (2) is
 * not supported by the driver" (Pi cycle m4b-xorg-drm).
 *
 * libphoenix also exports real functions for every one of these, so dropping
 * the macros gives correct single-evaluation calls.  Remove this file when
 * libphoenix's macros evaluate their argument once.
 */
#include_next <ctype.h>

#undef islower
#undef isupper
#undef isalpha
#undef iscntrl
#undef isdigit
#undef isalnum
#undef isprint
#undef isgraph
#undef ispunct
#undef isspace
#undef isxdigit
#undef isblank
#undef tolower
#undef toupper
#undef isascii
