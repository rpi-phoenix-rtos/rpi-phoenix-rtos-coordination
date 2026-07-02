#!/usr/bin/env bash
#
# Phoenix-RTOS — build xlogo (the X Window System logo) for aarch64-phoenix.
#
# xlogo is the canonical tiny toolkit demo: an Xt/Xaw client that draws the X
# logo. It can optionally use Xrender+Xft for an anti-aliased logo; this stack
# has NO Xft/fontconfig (see build-xterm.sh's --disable-freetype), so we build
# with --without-render — the core-X (non-antialiased) logo, which needs only
# libXaw/Xmu/Xt/Xext over libX11. Renders as an X client on the Xphoenix kdrive
# fbdev server (DISPLAY=:0), alongside twm/xterm.
#
# SOURCE: xorg app release tarball xlogo-1.0.7. Host-side build only. Idempotent.
#
# Copyright 2026 Phoenix Systems
# Author: Witold Bołt
set -u

APP=xlogo
VER=1.0.7
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
NFS=/srv/phoenix-rpi4-nfs

export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:$PREFIX/share/pkgconfig"
export PKG_CONFIG_LIBDIR="$PREFIX/lib/pkgconfig:$PREFIX/share/pkgconfig"

APP_CFLAGS="-DMAXHOSTNAMELEN=256 -DO_NOFOLLOW=0 -DXOS_USE_MTSAFE_PWDAPI -D_POSIX_THREAD_SAFE_FUNCTIONS=200809L"
# Full Xaw toolkit closure (Xaw<->Xmu<->Xt<->X11 circular -> start/end-group),
# appended after the objects by overriding the program's *_LDADD make variable.
# -lxkbfile supplies XkbStdBell() (the alarm-bell helper xlogo's quit() calls
# when configure detects xkbfile.pc); it lives in libxkbfile, not libX11.
XCLOSURE="-Wl,--start-group -lXaw7 -lXmu -lXt -lSM -lICE -lXpm -lXext -lxkbfile -lX11 -lxcb -lXau -lXdmcp -lphoenix -lc -lm -Wl,--end-group"

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
	    --without-render \
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
echo "HW: in an xterm under twm:  xlogo &"
