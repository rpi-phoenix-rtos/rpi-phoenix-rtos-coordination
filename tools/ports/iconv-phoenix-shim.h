/*
 * Phoenix-RTOS compatibility shim for GNU libiconv's bundled gnulib
 * (force-included via gcc -include).
 *
 * gnulib installs REPLACEMENT <stdint.h>/<time.h>/<stdio.h> that pull in
 * Phoenix's system headers "bare" (via #include_next), skipping the
 * <sys/types.h> that normal code drags in transitively. Phoenix's headers are
 * include-order-fragile: <time.h> references clock_t/clockid_t and <stdio.h>
 * references ssize_t, all of which live in <sys/types.h>. Force-including
 * <sys/types.h> before every TU defines those base types up front, killing the
 * "unknown type name 'clock_t'/'ssize_t'" errors regardless of gnulib's
 * header interposition.
 */
#ifndef ICONV_PHOENIX_SHIM_H
#define ICONV_PHOENIX_SHIM_H

#include <sys/types.h>

#endif /* ICONV_PHOENIX_SHIM_H */
