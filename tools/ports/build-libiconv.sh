#!/usr/bin/env bash
#
# Phoenix-RTOS — provide a static libiconv.a + <iconv.h> for aarch64-phoenix so
# downstream ports (glib2 -> Midnight Commander) get the iconv ABI. Phoenix libc
# has NO iconv.
#
# IMPLEMENTATION NOTE — why a stub, not GNU libiconv:
#   GNU libiconv 1.15's bundled gnulib refuses to cross-compile against Phoenix's
#   include-order-fragile system headers. gnulib installs REPLACEMENT
#   stdint.h/stdio.h/time.h/sys/types.h (via #include_next) which trip a circular
#   include chain: gnulib sys/types.h -> system sys/types.h -> <stdint.h> ->
#   gnulib stdint.h -> gnulib stdio.h -> system stdio.h, which references off_t
#   before the in-flight system sys/types.h has defined it ("unknown type name
#   'off_t'/'clock_t'"). Disabling individual gnulib header replacements
#   (gl_cv_header_working_stdint_h=yes, ...) does not stop generation cleanly;
#   untangling it is a deep rabbit hole that blocks the real deliverable (glib2).
#
#   So this builds a small SELF-WRITTEN stub libiconv.a (tools/ports/iconv-stub/)
#   implementing iconv_open/iconv/iconv_close as an ASCII/UTF-8 identity copy with
#   correct SUSv4 pointer/count + E2BIG semantics. This satisfies glib's iconv
#   detection + link and works for mc's default UTF-8/ASCII traffic. It does NOT
#   transcode legacy single-byte codepages. Replace with a real libiconv port for
#   full charset fidelity (TODO for the main session).
#
# Output: $PREFIX/lib/libiconv.a + $PREFIX/include/iconv.h, staged into the cross
# sysroot so dependent ports find -liconv + <iconv.h> automatically.
#
# Copyright 2026 Phoenix Systems
# Author: Witold Bołt
set -u

TC=/home/houp/phoenix-rpi/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-
SYSROOT=/home/houp/phoenix-rpi/.buildroot/_build/aarch64a72-generic-rpi4b/sysroot
PREFIX=/tmp/phoenix-iconv
HERE=/home/houp/phoenix-rpi/tools/ports
STUB=$HERE/iconv-stub

fail() { echo "FAIL: $*"; exit 1; }

mkdir -p "$PREFIX/lib" "$PREFIX/include"

echo "=== building stub libiconv.a ==="
${TC}gcc --sysroot="$SYSROOT" -O2 -c "$STUB/iconv.c" -I"$STUB" -o /tmp/iconv-stub.o \
	|| fail "stub compile failed"
${TC}ar rcs "$PREFIX/lib/libiconv.a" /tmp/iconv-stub.o || fail "ar failed"
${TC}ranlib "$PREFIX/lib/libiconv.a" 2>/dev/null || true
cp -a "$STUB/iconv.h" "$PREFIX/include/iconv.h"

[ -f "$PREFIX/lib/libiconv.a" ] || fail "libiconv.a not produced"

# Make the library + header visible to dependent cross-builds.
cp -a "$PREFIX"/lib/libiconv.a "$SYSROOT/lib/" 2>/dev/null || true
cp -a "$PREFIX"/include/iconv.h "$SYSROOT/usr/include/" 2>/dev/null || true

echo "=== libiconv (stub) OK ==="
ls -la "$PREFIX"/lib/libiconv.a
${TC}nm "$PREFIX/lib/libiconv.a" 2>/dev/null | grep -E " T (iconv_open|iconv|iconv_close)" | sed 's/^/  /'
