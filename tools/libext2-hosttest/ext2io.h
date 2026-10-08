/* Positional write for the host harnesses.
 *
 * Since the 2026-10 upstream sync ext2_write() takes the offset BY POINTER and
 * reports the new file offset back through it (the server answers mtWrite with
 * o.io.offs), and its mode argument carries O_APPEND, which makes it write at
 * the end of the file whatever offset was passed in:
 *
 *   ssize_t ext2_write(ext2_t *fs, id_t id, off_t *offs, const char *buff,
 *                      size_t len, unsigned int mode);
 *
 * The harnesses write at explicit offsets, as pwrite() does. ext2_pwrite()
 * keeps their calls readable and checks the new contract on EVERY write: the
 * reported offset must be the passed one plus the bytes written (unchanged on
 * an error). A wrong offset aborts, so the harness exits non-zero.
 *
 * O_APPEND is tested directly with ext2_write() (harness.c, stress.c).
 */
#ifndef EXT2IO_H
#define EXT2IO_H

#include <stdio.h>
#include <stdlib.h>

#include "ext2.h"


static inline ssize_t ext2_pwrite(ext2_t *fs, id_t id, off_t offs, const char *buff, size_t len)
{
	off_t pos = offs;
	ssize_t ret = ext2_write(fs, id, &pos, buff, len, 0);
	off_t want = offs + ((ret > 0) ? ret : 0);

	if (pos != want) {
		fprintf(stderr, "ext2_write(id %llu, offs %lld, len %zu) = %zd reported offset %lld, expected %lld\n",
			(unsigned long long)id, (long long)offs, len, ret, (long long)pos, (long long)want);
		abort();
	}

	return ret;
}

#endif
