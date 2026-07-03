#!/usr/bin/env bash
#
# Phoenix-RTOS — build ico (the spinning wireframe polyhedron) for aarch64-phoenix.
#
# ico is a tiny pure-Xlib demo client (a single ico.c) that bounces/rotates a
# wireframe polyhedron — very demo-friendly and the lowest-risk app in the X11
# sample set: it needs only core X11 (libX11/xcb), no toolkit, no fonts. It
# links statically against the X11 client stack already cross-compiled into
# $PREFIX by build-x11-phoenix.sh and renders as an X client on the Xphoenix
# kdrive fbdev server (DISPLAY=:0), alongside twm/xterm.
#
# SOURCE: the xorg app release tarball ico-1.0.6 (ships a generated ./configure).
# Recipe mirrors the xeyes autotools build (see apps/build.sh / config.log), not
# the hand-rolled build-xterm.sh.
#
# Host-side build only (does NOT boot the Pi, does NOT touch the flagship image).
# Idempotent: skips fetch/extract/configure/build when outputs already exist.
#
# Copyright 2026 Phoenix Systems
# Author: Witold Bołt
set -u

APP=ico
VER=1.0.6
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

# pkg-config: prefix .pc only (LIBDIR, not just PATH) so host /usr/lib *.pc are
# invisible to the cross configure.
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:$PREFIX/share/pkgconfig"
export PKG_CONFIG_LIBDIR="$PREFIX/lib/pkgconfig:$PREFIX/share/pkgconfig"

# The libphoenix CFLAGS quirks that the xeyes/xterm builds also use:
# MAXHOSTNAMELEN + O_NOFOLLOW (libphoenix lacks them), the MT-safe pwd API knobs.
APP_CFLAGS="-DMAXHOSTNAMELEN=256 -DO_NOFOLLOW=0 -DXOS_USE_MTSAFE_PWDAPI -D_POSIX_THREAD_SAFE_FUNCTIONS=200809L"

# Static X11 link closure, ordered, appended AFTER the objects. Core X only.
XCLOSURE="-lX11 -lxcb -lXau -lXdmcp -lphoenix -lc"

fail() { echo "FAIL: $*"; exit 1; }

# --- 0. prerequisite: the X11 prefix must already be populated ---
[ -f "$PREFIX/lib/libX11.a" ] || { "$TOOLS/build-x11-phoenix.sh" || fail "build-x11-phoenix.sh failed"; }

# --- 1. fetch + extract the release tarball ---
mkdir -p "$SRC"
if [ ! -d "$XDIR" ]; then
	[ -f "$SRC/$TARBALL" ] || { echo "=== fetching $URL ==="; curl -sSL --max-time 90 -o "$SRC/$TARBALL" "$URL" || fail "download failed"; }
	echo "=== extracting $TARBALL ==="
	tar -C "$SRC" -xf "$SRC/$TARBALL" || fail "extract failed"
fi
[ -f "$XDIR/configure" ] || fail "$XDIR/configure missing"

# --- 2. configure (core X only; mirrors the xeyes cross invocation) ---
if [ ! -f "$XDIR/config.status" ]; then
	echo "=== configuring $NV ==="
	( cd "$XDIR" && ./configure --host=aarch64-phoenix --prefix="$PREFIX" \
	    CC=${TC}gcc AR=${TC}ar RANLIB=${TC}ranlib \
	    CFLAGS="--sysroot=$SYSROOT -I$PREFIX/include $APP_CFLAGS" \
	    LDFLAGS="--sysroot=$SYSROOT -static -L$PREFIX/lib -L$SYSROOT/lib" \
	    >/tmp/$APP-conf.log 2>&1 ) || { tail -25 /tmp/$APP-conf.log; fail "configure failed"; }
fi

# --- 3. build (force the static X closure after the objects via LDFLAGS append) ---
if [ ! -x "$XDIR/$APP" ]; then
	echo "=== building $NV ==="
	( cd "$XDIR" && make \
	    CFLAGS="--sysroot=$SYSROOT -I$PREFIX/include $APP_CFLAGS" \
	    LDFLAGS="--sysroot=$SYSROOT -static -L$PREFIX/lib -L$SYSROOT/lib" \
	    ICO_LIBS="-Wl,--start-group $XCLOSURE -Wl,--end-group" \
	    >/tmp/$APP-build.log 2>&1 ) || { tail -30 /tmp/$APP-build.log; fail "make failed"; }
fi
[ -x "$XDIR/$APP" ] || fail "$APP not produced"

# --- 4. publish + stage ---
mkdir -p "$ART"
cp "$XDIR/$APP" "$ART/$APP"
echo "=== published -> $ART/$APP ==="
if [ -d "$NFS/bin" ]; then
	cp "$XDIR/$APP" "$NFS/bin/$APP"; chmod 755 "$NFS/bin/$APP"
	echo "=== staged -> $NFS/bin/$APP ==="
fi

# --- 5. pre-flight checks (no Pi cycle) ---
echo "=== PRE-FLIGHT ==="
file "$ART/$APP"
case "$(file "$ART/$APP")" in
	*"ARM aarch64"*"statically linked"*) echo "[OK] aarch64 static ELF" ;;
	*) fail "binary is not an aarch64 static ELF" ;;
esac
und=$(${TC}nm -u "$ART/$APP" 2>/dev/null)
[ -z "$und" ] && echo "[OK] 0 undefined symbols" || { echo "$und"; fail "undefined symbols present"; }
echo "=== ALL PRE-FLIGHT CHECKS PASSED ==="
echo "HW: in an xterm under twm:  ico -faces -sleep 1 &   (or run via startx)"
