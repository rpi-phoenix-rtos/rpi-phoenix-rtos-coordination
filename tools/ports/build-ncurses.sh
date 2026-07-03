#!/usr/bin/env bash
#
# Phoenix-RTOS — cross-build ncurses for aarch64-phoenix as a REUSABLE static
# library (libncurses.a + headers) that other ports (GNU nano, vim, mc, ...)
# link against. Self-contained: no Phoenix-ports framework dependency.
#
# Phoenix has no terminfo database on disk, so we compile the terminal
# descriptions INTO the library with --with-fallbacks (xterm/vt100/linux/ansi/...
# cover the Pi UART+fbcon console and a host xterm). setupterm() then resolves
# TERM from the compiled-in fallback, no /usr/share/terminfo needed at runtime.
#
# Output: $PREFIX/lib/lib{ncurses,tinfo}.a + $PREFIX/include/ncurses/*.h, also
# staged into the cross sysroot so dependent ports find -lncurses automatically.
#
# Copyright 2026 Phoenix Systems
# Author: Witold Bołt
set -u

NV=ncurses-6.4
URL=https://ftp.gnu.org/gnu/ncurses/$NV.tar.gz

# Repo root derived from this script's own location (portable across checkouts).
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/../.." && pwd)"

TC=${ROOT}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-
SYSROOT=${ROOT}/.buildroot/_build/aarch64a72-generic-rpi4b/sysroot
PREFIX=/tmp/phoenix-ncurses
SRC=${ROOT}/tools/ports/src
XDIR=$SRC/$NV
NFS="${SHOWCASE_STAGE_DIR:-/srv/phoenix-rpi4-nfs}"

# Terminals to bake in: the Pi console (vt100/linux/ansi, now with SGR colour),
# plus xterm variants for a remote/host terminal.
FALLBACKS="xterm,xterm-256color,vt100,vt220,linux,ansi,dumb,screen"

fail() { echo "FAIL: $*"; exit 1; }

mkdir -p "$SRC"
if [ ! -d "$XDIR" ]; then
	[ -f "$SRC/$NV.tar.gz" ] || { echo "=== fetching $URL ==="; curl -sSL --max-time 120 -o "$SRC/$NV.tar.gz" "$URL" || fail "download failed"; }
	tar -C "$SRC" -xf "$SRC/$NV.tar.gz" || fail "extract failed"
fi

if [ ! -f "$XDIR/config.status" ]; then
	echo "=== configuring $NV (fallbacks: $FALLBACKS) ==="
	( cd "$XDIR" && ./configure \
	    --host=aarch64-phoenix --build=x86_64-pc-linux-gnu --prefix="$PREFIX" \
	    --without-shared --without-debug --without-tests --without-progs \
	    --without-cxx --without-cxx-binding --without-ada --without-manpages \
	    --disable-db-install --with-fallbacks="$FALLBACKS" \
	    --enable-termcap --disable-home-terminfo --enable-sp-funcs \
	    --without-pkg-config \
	    CC=${TC}gcc AR=${TC}ar RANLIB=${TC}ranlib \
	    CForge= CFLAGS="--sysroot=$SYSROOT -O2" \
	    CPPFLAGS="--sysroot=$SYSROOT" LDFLAGS="--sysroot=$SYSROOT" \
	    >/tmp/ncurses-conf.log 2>&1 ) || { tail -30 /tmp/ncurses-conf.log; fail "configure failed"; }
fi

echo "=== building $NV ==="
( cd "$XDIR" && make >/tmp/ncurses-build.log 2>&1 && make install >/tmp/ncurses-install.log 2>&1 ) \
	|| { tail -30 /tmp/ncurses-build.log /tmp/ncurses-install.log; fail "build failed"; }

[ -f "$PREFIX/lib/libncurses.a" ] || fail "libncurses.a not produced"

# Make the library + headers visible to dependent cross-builds.
cp -a "$PREFIX"/lib/lib*.a "$SYSROOT/lib/" 2>/dev/null || true
mkdir -p "$SYSROOT/include/ncurses"
cp -a "$PREFIX"/include/* "$SYSROOT/include/" 2>/dev/null || true
cp -a "$PREFIX"/include/* "$SYSROOT/include/ncurses/" 2>/dev/null || true

echo "=== ncurses OK ==="
ls -la "$PREFIX"/lib/lib*.a
${TC}nm "$PREFIX/lib/libncurses.a" 2>/dev/null | grep -cE " T (initscr|setupterm|newwin|tputs)" | sed 's/^/core symbols present: /'
echo "(static libncurses.a with compiled-in terminfo fallbacks — link nano with -lncurses)"
