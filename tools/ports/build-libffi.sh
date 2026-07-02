#!/usr/bin/env bash
#
# Phoenix-RTOS — cross-build libffi 3.3 for aarch64-phoenix as a REUSABLE static
# library (libffi.a + ffi.h). Needed by glib2's gobject (gclosure marshalling);
# glib-2.56 configure HARD-requires libffi even for a core build.
#
# Self-contained, follows the ncurses template. libffi 3.3's config.sub already
# knows "phoenix". The aarch64 sysv backend (src/aarch64/) is pure asm+C and has
# no OS dependency beyond a working <stdlib.h>/<string.h>, so it cross-compiles
# cleanly. Closures need executable mmap; the static .a still links fine and
# ffi_call works regardless (closures are only exercised by gobject signals).
#
# Output: $PREFIX/lib/libffi.a + $PREFIX/include/ffi.h + ffitarget.h, staged into
# the cross sysroot so glib2 configure finds LIBFFI without pkg-config.
#
# Copyright 2026 Phoenix Systems
# Author: Witold Bołt
set -u

NV=libffi-3.3
URL=https://github.com/libffi/libffi/releases/download/v3.3/$NV.tar.gz

# Repo root derived from this script's own location (portable across checkouts).
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/../.." && pwd)"

TC=${ROOT}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-
SYSROOT=${ROOT}/.buildroot/_build/aarch64a72-generic-rpi4b/sysroot
PREFIX=/tmp/phoenix-ffi
HERE=${ROOT}/tools/ports
SRC=$HERE/src
XDIR=$SRC/$NV

fail() { echo "FAIL: $*"; exit 1; }

mkdir -p "$SRC"
if [ ! -d "$XDIR" ]; then
	[ -f "$SRC/$NV.tar.gz" ] || { echo "=== fetching $URL ==="; curl -sSL --max-time 180 -o "$SRC/$NV.tar.gz" "$URL" || fail "download failed"; }
	tar -C "$SRC" -xf "$SRC/$NV.tar.gz" || fail "extract failed"
fi

for cfg in config.sub config.guess; do
	if ! grep -q phoenix "$XDIR/$cfg" 2>/dev/null; then
		s=$(grep -lr phoenix ${ROOT}/tools/x11-port/src/*/$cfg 2>/dev/null | head -1)
		[ -n "$s" ] && cp "$s" "$XDIR/$cfg" && echo "=== refreshed $cfg ==="
	fi
done

CF="--sysroot=$SYSROOT -O2"
if [ ! -f "$XDIR/config.status" ]; then
	echo "=== configuring $NV ==="
	( cd "$XDIR" && ./configure \
	    --host=aarch64-phoenix --build=x86_64-pc-linux-gnu --prefix="$PREFIX" \
	    --enable-static --disable-shared --disable-docs --disable-multi-os-directory \
	    CC=${TC}gcc AR=${TC}ar RANLIB=${TC}ranlib \
	    CFLAGS="$CF" CPPFLAGS="--sysroot=$SYSROOT" LDFLAGS="--sysroot=$SYSROOT" \
	    >/tmp/ffi-conf.log 2>&1 ) || { tail -n 40 /tmp/ffi-conf.log; fail "configure failed"; }
fi

echo "=== building $NV ==="
( cd "$XDIR" && make >/tmp/ffi-build.log 2>&1 && make install >/tmp/ffi-install.log 2>&1 ) \
	|| { tail -n 50 /tmp/ffi-build.log; fail "build failed"; }

# libffi installs to PREFIX/lib (or lib64); find the .a
FA=$(grep -rl "" "$PREFIX" --include=libffi.a 2>/dev/null | head -1)
[ -n "$FA" ] || FA="$PREFIX/lib/libffi.a"
[ -f "$FA" ] || fail "libffi.a not produced"

# ffi.h/ffitarget.h land in PREFIX/include OR PREFIX/lib/libffi-3.3/include
FH=$(grep -rl "" "$PREFIX" --include=ffi.h 2>/dev/null | head -1)
FHDIR=$(dirname "$FH")

cp -a "$FA" "$SYSROOT/lib/libffi.a"
cp -a "$FHDIR"/ffi.h "$FHDIR"/ffitarget.h "$SYSROOT/usr/include/" 2>/dev/null || true
# Also keep a clean PREFIX/lib + PREFIX/include layout for downstream -I/-L use.
mkdir -p "$PREFIX/lib" "$PREFIX/include"
cp -a "$FA" "$PREFIX/lib/libffi.a"
cp -a "$FHDIR"/ffi.h "$FHDIR"/ffitarget.h "$PREFIX/include/" 2>/dev/null || true

echo "=== libffi OK ==="
ls -la "$SYSROOT/lib/libffi.a"
${TC}nm "$SYSROOT/lib/libffi.a" 2>/dev/null | grep -E " T (ffi_call|ffi_prep_cif|ffi_prep_closure_loc)$" | sed 's/^/  /'
