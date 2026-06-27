#!/usr/bin/env bash
#
# Phoenix-RTOS — build oclock (the round analog clock) for aarch64-phoenix.
#
# oclock is a small core-X client (a shaped round-window clock, no toolkit
# widgets) using libXmu/libXext/libXt over libX11 — no Xft/fontconfig, so it
# builds against the existing X11 prefix (build-x11-phoenix.sh) with the same
# autotools cross recipe as xeyes. Renders as an X client on the Xphoenix
# kdrive fbdev server (DISPLAY=:0), alongside twm/xterm.
#
# SOURCE: xorg app release tarball oclock-1.0.5 (ships a generated ./configure).
# Host-side build only. Idempotent.
#
# Copyright 2026 Phoenix Systems
# Author: Witold Bołt
set -u

APP=oclock
VER=1.0.5
NV=$APP-$VER
TARBALL=$NV.tar.gz
URL=http://www.x.org/archive/individual/app/$TARBALL

TC=/home/houp/phoenix-rpi/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-
SYSROOT=/home/houp/phoenix-rpi/.buildroot/_build/aarch64a72-generic-rpi4b/sysroot
PREFIX=/tmp/x11-phoenix
TOOLS=/home/houp/phoenix-rpi/tools/x11-port
SRC=$TOOLS/src
XDIR=$SRC/$NV
ART=/home/houp/phoenix-rpi/artifacts/x11
NFS=/srv/phoenix-rpi4-nfs

export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:$PREFIX/share/pkgconfig"
export PKG_CONFIG_LIBDIR="$PREFIX/lib/pkgconfig:$PREFIX/share/pkgconfig"

APP_CFLAGS="-DMAXHOSTNAMELEN=256 -DO_NOFOLLOW=0 -DXOS_USE_MTSAFE_PWDAPI -D_POSIX_THREAD_SAFE_FUNCTIONS=200809L"
# Ordered static closure (Xt<->Xmu<->X11 circular -> start/end-group), appended
# after the objects by overriding the program's *_LDADD make variable.
XCLOSURE="-Wl,--start-group -lXmu -lXt -lSM -lICE -lXext -lX11 -lxcb -lXau -lXdmcp -lphoenix -lc -lm -Wl,--end-group"

# --without-xkb: oclock's only XKB use is XkbStdBell() for the alarm bell, which
# is NOT in this libX11 port (it historically lived in libxkbfile/utils and is
# absent here). With --without-xkb oclock's quit() falls back to plain XBell(),
# which libX11 provides — the round clock itself is unaffected.

fail() { echo "FAIL: $*"; exit 1; }

[ -f "$PREFIX/lib/libX11.a" ] || { "$TOOLS/build-x11-phoenix.sh" || fail "build-x11-phoenix.sh failed"; }

mkdir -p "$SRC"
if [ ! -d "$XDIR" ]; then
	[ -f "$SRC/$TARBALL" ] || { echo "=== fetching $URL ==="; curl -sSL --max-time 90 -o "$SRC/$TARBALL" "$URL" || fail "download failed"; }
	tar -C "$SRC" -xf "$SRC/$TARBALL" || fail "extract failed"
fi
[ -f "$XDIR/configure" ] || fail "$XDIR/configure missing"

if [ ! -f "$XDIR/config.status" ]; then
	echo "=== configuring $NV ==="
	( cd "$XDIR" && ./configure --host=aarch64-phoenix --prefix="$PREFIX" \
	    --without-xkb \
	    CC=${TC}gcc AR=${TC}ar RANLIB=${TC}ranlib \
	    CFLAGS="--sysroot=$SYSROOT -I$PREFIX/include $APP_CFLAGS" \
	    LDFLAGS="--sysroot=$SYSROOT -static -L$PREFIX/lib -L$SYSROOT/lib" \
	    >/tmp/$APP-conf.log 2>&1 ) || { tail -25 /tmp/$APP-conf.log; fail "configure failed"; }
fi

if [ ! -x "$XDIR/$APP" ]; then
	echo "=== building $NV ==="
	( cd "$XDIR" && make ${APP}_LDADD="$XCLOSURE" \
	    CFLAGS="--sysroot=$SYSROOT -I$PREFIX/include $APP_CFLAGS" \
	    LDFLAGS="--sysroot=$SYSROOT -static -L$PREFIX/lib -L$SYSROOT/lib" \
	    >/tmp/$APP-build.log 2>&1 ) || { tail -30 /tmp/$APP-build.log; fail "make failed"; }
fi
[ -x "$XDIR/$APP" ] || fail "$APP not produced"

mkdir -p "$ART"; cp "$XDIR/$APP" "$ART/$APP"
echo "=== published -> $ART/$APP ==="
if [ -d "$NFS/bin" ]; then cp "$XDIR/$APP" "$NFS/bin/$APP"; chmod 755 "$NFS/bin/$APP"; echo "=== staged -> $NFS/bin/$APP ==="; fi

echo "=== PRE-FLIGHT ==="
file "$ART/$APP"
case "$(file "$ART/$APP")" in
	*"ARM aarch64"*"statically linked"*) echo "[OK] aarch64 static ELF" ;;
	*) fail "binary is not an aarch64 static ELF" ;;
esac
und=$(${TC}nm -u "$ART/$APP" 2>/dev/null)
[ -z "$und" ] && echo "[OK] 0 undefined symbols" || { echo "$und"; fail "undefined symbols present"; }
echo "=== ALL PRE-FLIGHT CHECKS PASSED ==="
echo "HW: in an xterm under twm:  oclock &"
