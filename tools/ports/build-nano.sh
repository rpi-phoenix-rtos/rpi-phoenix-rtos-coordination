#!/usr/bin/env bash
#
# Phoenix-RTOS — cross-build GNU nano for aarch64-phoenix against the ported
# ncurses (build-ncurses.sh must run first). Static binary -> NFS /bin/nano.
#
# nano 2.2.x is used deliberately: it bundles NO gnulib, so it sidesteps the
# gnulib-vs-Phoenix namespace collisions (gettime/getprogname/...) that block the
# modern (6.x) nano. The only Phoenix gaps are P_tmpdir + the passwd-enumeration
# API, supplied by the force-included nano-phoenix-shim.h.
#
# Copyright 2026 Phoenix Systems
# Author: Witold Bołt
set -u

NV=nano-2.2.6
URL=https://www.nano-editor.org/dist/v2.2/$NV.tar.gz

TC=/home/houp/phoenix-rpi/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-
SYSROOT=/home/houp/phoenix-rpi/.buildroot/_build/aarch64a72-generic-rpi4b/sysroot
NCPREFIX=/tmp/phoenix-ncurses
HERE=/home/houp/phoenix-rpi/tools/ports
SRC=$HERE/src
XDIR=$SRC/$NV
SHIM=$HERE/nano-phoenix-shim.h
NFS=/srv/phoenix-rpi4-nfs

fail() { echo "FAIL: $*"; exit 1; }

[ -f "$NCPREFIX/lib/libncurses.a" ] || { "$HERE/build-ncurses.sh" || fail "build-ncurses.sh failed"; }

mkdir -p "$SRC"
if [ ! -d "$XDIR" ]; then
	[ -f "$SRC/$NV.tar.gz" ] || { echo "=== fetching $URL ==="; curl -sSL --max-time 120 -o "$SRC/$NV.tar.gz" "$URL" || fail "download failed"; }
	tar -C "$SRC" -xf "$SRC/$NV.tar.gz" || fail "extract failed"
fi

# Refresh config.sub/guess if too old to know "phoenix".
for cfg in config.sub config.guess; do
	if ! grep -q phoenix "$XDIR/$cfg" 2>/dev/null; then
		s=$(grep -lr phoenix /home/houp/phoenix-rpi/tools/x11-port/src/*/$cfg 2>/dev/null | head -1)
		[ -n "$s" ] && cp "$s" "$XDIR/$cfg" && echo "=== refreshed $cfg ==="
	fi
done

CF="--sysroot=$SYSROOT -O2 -I$NCPREFIX/include -I$NCPREFIX/include/ncurses -include $SHIM"
if [ ! -f "$XDIR/config.status" ]; then
	echo "=== configuring $NV ==="
	( cd "$XDIR" && ./configure \
	    --host=aarch64-phoenix --build=x86_64-pc-linux-gnu --disable-nls --disable-utf8 \
	    CC=${TC}gcc AR=${TC}ar RANLIB=${TC}ranlib \
	    CPPFLAGS="--sysroot=$SYSROOT -I$NCPREFIX/include -I$NCPREFIX/include/ncurses" \
	    CFLAGS="$CF" \
	    LDFLAGS="--sysroot=$SYSROOT -static -L$NCPREFIX/lib" LIBS="-lncurses" \
	    ac_cv_lib_ncursesw_initscr=no ac_cv_lib_ncurses_initscr=yes \
	    >/tmp/nano-conf.log 2>&1 ) || { tail -25 /tmp/nano-conf.log; fail "configure failed"; }
fi

echo "=== building $NV ==="
( cd "$XDIR" && make CFLAGS="$CF" >/tmp/nano-build.log 2>&1 ) || { tail -25 /tmp/nano-build.log; fail "make failed"; }

BIN="$XDIR/src/nano"
[ -x "$BIN" ] || fail "src/nano not produced"
mkdir -p /home/houp/phoenix-rpi/artifacts; cp "$BIN" /home/houp/phoenix-rpi/artifacts/nano
if [ -d "$NFS/bin" ]; then
	cp "$BIN" "$NFS/bin/nano"; chmod 755 "$NFS/bin/nano"
	echo "=== staged -> $NFS/bin/nano ==="
fi

echo "=== PRE-FLIGHT ==="
file "$BIN"
case "$(file "$BIN")" in
	*"ARM aarch64"*"statically linked"*) echo "[OK] aarch64 static ELF" ;;
	*) fail "not an aarch64 static ELF" ;;
esac
und=$(${TC}nm -u "$BIN" 2>/dev/null)
[ -z "$und" ] && echo "[OK] 0 undefined symbols" || { echo "$und" | head; fail "undefined symbols present"; }
echo "=== nano OK === (HW: TERM=vt100 nano /etc/hostname)"
