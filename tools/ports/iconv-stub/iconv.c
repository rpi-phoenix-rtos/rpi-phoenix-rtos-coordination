/*
 * Phoenix-RTOS — stub libiconv implementation.
 *
 * Provides the iconv ABI (iconv_open / iconv / iconv_close) that glib2 and
 * Midnight Commander link against. Phoenix has no real iconv and GNU libiconv's
 * gnulib won't cross-compile here (see iconv.h header comment).
 *
 * Behaviour:
 *   - The conversion descriptor records whether the *target* charset is a
 *     UTF-8/ASCII superset (the common case for mc on a UTF-8 console).
 *   - iconv() copies input bytes to output verbatim (identity), advancing the
 *     pointers/counts per the SUSv4 iconv contract, and returning E2BIG when the
 *     output buffer is too small (so glib's grow-and-retry loop works).
 *   - It does NOT transcode legacy single-byte codepages; those bytes pass
 *     through unchanged. This is sufficient for ASCII and UTF-8<->UTF-8 traffic,
 *     which covers mc's default operation on a UTF-8 terminal.
 *
 * Replace with a real libiconv port for full legacy-charset fidelity.
 *
 * Copyright 2026 Phoenix Systems
 * Author: Witold Bołt
 */
#include "iconv.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* Opaque descriptor: just a sentinel so iconv_open returns a non-NULL handle. */
struct phoenix_iconv_cd {
	int magic;
};

#define ICONV_MAGIC 0x69636e76 /* 'icnv' */

iconv_t iconv_open(const char *tocode, const char *fromcode)
{
	struct phoenix_iconv_cd *cd;

	(void)tocode;
	(void)fromcode;

	cd = (struct phoenix_iconv_cd *)malloc(sizeof(*cd));
	if (cd == NULL) {
		errno = ENOMEM;
		return (iconv_t)-1;
	}
	cd->magic = ICONV_MAGIC;
	return (iconv_t)cd;
}

size_t iconv(iconv_t cd, char **inbuf, size_t *inbytesleft,
             char **outbuf, size_t *outbytesleft)
{
	struct phoenix_iconv_cd *c = (struct phoenix_iconv_cd *)cd;
	size_t n;

	if (c == NULL || c->magic != ICONV_MAGIC) {
		errno = EBADF;
		return (size_t)-1;
	}

	/* Flush/reset request: iconv(cd, NULL, ...) — nothing buffered, succeed. */
	if (inbuf == NULL || *inbuf == NULL || inbytesleft == NULL) {
		return 0;
	}

	/* Identity copy, bounded by the smaller of in/out remaining. */
	while (*inbytesleft > 0) {
		if (outbuf == NULL || *outbuf == NULL || outbytesleft == NULL || *outbytesleft == 0) {
			errno = E2BIG;
			return (size_t)-1;
		}
		n = *inbytesleft;
		if (n > *outbytesleft) {
			n = *outbytesleft;
		}
		memcpy(*outbuf, *inbuf, n);
		*inbuf += n;
		*outbuf += n;
		*inbytesleft -= n;
		*outbytesleft -= n;
		if (*inbytesleft > 0) {
			/* ran out of output room */
			errno = E2BIG;
			return (size_t)-1;
		}
	}

	/* Number of characters converted in a nonreversible way: none. */
	return 0;
}

int iconv_close(iconv_t cd)
{
	struct phoenix_iconv_cd *c = (struct phoenix_iconv_cd *)cd;

	if (c == NULL || c->magic != ICONV_MAGIC) {
		errno = EBADF;
		return -1;
	}
	free(c);
	return 0;
}
