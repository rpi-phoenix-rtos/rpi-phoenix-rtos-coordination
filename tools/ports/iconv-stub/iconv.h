/*
 * Phoenix-RTOS — minimal <iconv.h> for the stub libiconv.a.
 *
 * Phoenix libc ships no iconv. GNU libiconv 1.15's bundled gnulib refuses to
 * cross-compile against Phoenix's include-order-fragile headers (a circular
 * sys/types.h -> stdint.h -> stdio.h -> off_t chain that gnulib's header
 * interposition triggers). Rather than untangle gnulib, this stub provides the
 * iconv ABI glib2 (and downstream Midnight Commander) link against, backed by an
 * ASCII/UTF-8/Latin-1 identity + a couple of common transliterations.
 *
 * LIMITATION: this is NOT a full charset-conversion engine. Conversions whose
 * source and target are both UTF-8-compatible (the mc default) pass through
 * byte-for-byte; everything else is treated as an identity copy. For full
 * legacy-codepage support, replace with a real libiconv port later.
 *
 * Copyright 2026 Phoenix Systems
 * Author: Witold Bołt
 */
#ifndef _PHOENIX_ICONV_H
#define _PHOENIX_ICONV_H

#include <sys/types.h>   /* size_t */

#ifdef __cplusplus
extern "C" {
#endif

typedef void *iconv_t;

iconv_t iconv_open(const char *tocode, const char *fromcode);
size_t  iconv(iconv_t cd, char **inbuf, size_t *inbytesleft,
              char **outbuf, size_t *outbytesleft);
int     iconv_close(iconv_t cd);

#ifdef __cplusplus
}
#endif

#endif /* _PHOENIX_ICONV_H */
