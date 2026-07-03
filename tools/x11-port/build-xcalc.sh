#!/usr/bin/env bash
#
# Phoenix-RTOS — build xcalc (the scientific calculator) for aarch64-phoenix.
#
# xcalc is an Xt/Xaw client (libXaw/Xmu/Xt/Xext over libX11). It has no Xft and
# no XKB option, so it links straight against the existing X11 prefix. Renders
# as an X client on the Xphoenix kdrive fbdev server (DISPLAY=:0), under twm.
#
# RESOURCES (IMPORTANT): xcalc's button layout, labels and key bindings live in
# its app-defaults file (the XCalc class file) — WITHOUT it xcalc comes up as a
# bare empty window. libXt's compiled default search path points at the host
# build prefix (/tmp/x11-phoenix), which does not exist on the Pi, so we stage
# XCalc to $NFS/usr/share/X11/app-defaults and the launch recipe MUST set
# XFILESEARCHPATH=/usr/share/X11/app-defaults/%N before running xcalc.
#
# SOURCE: xorg app release tarball xcalc-1.1.2. Host-side build only. Idempotent.
#
# Copyright 2026 Phoenix Systems
# Author: Witold Bołt
set -u

APP=xcalc
VER=1.1.2
NV=$APP-$VER
TARBALL=$NV.tar.gz
URL=http://www.x.org/archive/individual/app/$TARBALL

# Repo root derived from this script's own location (portable across checkouts).
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/../.." && pwd)"

TC=${ROOT}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-
SYSROOT=${ROOT}/.buildroot/_build/aarch64a72-generic-rpi4b/sysroot
PREFIX=/tmp/x11-phoenix
TOOLS=${ROOT}/tools/x11-port
SRC=$TOOLS/src
XDIR=$SRC/$NV
ART=${ROOT}/artifacts/x11
NFS="${SHOWCASE_STAGE_DIR:-/srv/phoenix-rpi4-nfs}"

export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:$PREFIX/share/pkgconfig"
export PKG_CONFIG_LIBDIR="$PREFIX/lib/pkgconfig:$PREFIX/share/pkgconfig"

APP_CFLAGS="-DMAXHOSTNAMELEN=256 -DO_NOFOLLOW=0 -DXOS_USE_MTSAFE_PWDAPI -D_POSIX_THREAD_SAFE_FUNCTIONS=200809L"
XCLOSURE="-Wl,--start-group -lXaw7 -lXmu -lXt -lSM -lICE -lXpm -lXext -lX11 -lxcb -lXau -lXdmcp -lphoenix -lc -lm -Wl,--end-group"

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
if [ -d "$NFS/bin" ]; then
	cp "$XDIR/$APP" "$NFS/bin/$APP"; chmod 755 "$NFS/bin/$APP"
	mkdir -p "$NFS/usr/share/X11/app-defaults"
	[ -f "$XDIR/app-defaults/XCalc" ] && cp "$XDIR/app-defaults/XCalc" "$NFS/usr/share/X11/app-defaults/XCalc"
	[ -f "$XDIR/app-defaults/XCalc-color" ] && cp "$XDIR/app-defaults/XCalc-color" "$NFS/usr/share/X11/app-defaults/XCalc-color"
	echo "=== staged -> $NFS/bin/$APP (+ app-defaults/XCalc) ==="
fi

echo "=== PRE-FLIGHT ==="
file "$ART/$APP"
case "$(file "$ART/$APP")" in
	*"ARM aarch64"*"statically linked"*) echo "[OK] aarch64 static ELF" ;;
	*) fail "binary is not an aarch64 static ELF" ;;
esac
und=$(${TC}nm -u "$ART/$APP" 2>/dev/null)
[ -z "$und" ] && echo "[OK] 0 undefined symbols" || { echo "$und"; fail "undefined symbols present"; }
echo "=== ALL PRE-FLIGHT CHECKS PASSED ==="
echo "HW: in an xterm under twm (app-defaults REQUIRED for the button layout):"
echo "    export XFILESEARCHPATH=/usr/share/X11/app-defaults/%N"
echo "    xcalc &"
