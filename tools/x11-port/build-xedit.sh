#!/usr/bin/env bash
#
# Phoenix-RTOS — build xedit (the Athena text editor) for aarch64-phoenix.
#
# xedit is an Xt/Xaw client (libXaw/Xmu/Xt over libX11) — a classic Athena-widget
# multi-buffer text editor that matches the Window Maker / twm look. It links
# straight against the existing X11 prefix (no Xft/XKB). Renders as an X client
# on the Xphoenix kdrive fbdev server (DISPLAY=:0).
#
# RESOURCES: xedit's UI (menus, key bindings, the edit/messages panes) lives in
# its app-defaults file (the Xedit class file). libXt's compiled default search
# path points at the host build prefix, so we stage Xedit to
# $NFS/usr/share/X11/app-defaults and the launch recipe MUST set
# XFILESEARCHPATH=/usr/share/X11/app-defaults/%N before running xedit.
#
# SOURCE: xorg app release tarball xedit-1.2.2. Host-side build only. Idempotent.
#
# Copyright 2026 Phoenix Systems
# Author: Witold Bołt
set -u

APP=xedit
VER=1.2.2
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

# -Dfinite=isfinite: xedit's bundled Lisp interpreter (lisp/mathimp.c) calls the
# obsolete BSD finite(); Phoenix libm only has C99 isfinite (a type-generic macro
# that accepts the same double argument).
SHIM=$TOOLS/xedit-phoenix-shim.h
APP_CFLAGS="-DMAXHOSTNAMELEN=256 -DO_NOFOLLOW=0 -DXOS_USE_MTSAFE_PWDAPI -D_POSIX_THREAD_SAFE_FUNCTIONS=200809L -Dfinite=isfinite -include $SHIM"
XCLOSURE="-Wl,--start-group -lXaw7 -lXmu -lXt -lSM -lICE -lXpm -lXext -lX11 -lxcb -lXau -lXdmcp -lphoenix -lc -lm -Wl,--end-group"

fail() { echo "FAIL: $*"; exit 1; }

[ -f "$PREFIX/lib/libX11.a" ] || { "$TOOLS/build-x11-phoenix.sh" || fail "build-x11-phoenix.sh failed"; }

mkdir -p "$SRC"
if [ ! -d "$XDIR" ]; then
	[ -f "$SRC/$TARBALL" ] || { echo "=== fetching $URL ==="; curl -sSL --max-time 90 -o "$SRC/$TARBALL" "$URL" || fail "download failed"; }
	tar -C "$SRC" -xf "$SRC/$TARBALL" || fail "extract failed"
fi
[ -f "$XDIR/configure" ] || fail "$XDIR/configure missing"

# xedit-1.2.2 ships a 2014 config.sub that predates the "phoenix" OS triplet.
# Refresh it (+ config.guess) from a newer xorg app tree so --host=aarch64-phoenix
# is accepted.
for cfg in config.sub config.guess; do
	if ! grep -q phoenix "$XDIR/$cfg" 2>/dev/null; then
		src=$(grep -lr phoenix "$SRC"/*/$cfg 2>/dev/null | head -1)
		[ -n "$src" ] && cp "$src" "$XDIR/$cfg" && echo "=== refreshed $cfg (phoenix-aware) ==="
	fi
done

if [ ! -f "$XDIR/config.status" ]; then
	echo "=== configuring $NV ==="
	( cd "$XDIR" && ./configure --host=aarch64-phoenix --prefix="$PREFIX" \
	    CC=${TC}gcc AR=${TC}ar RANLIB=${TC}ranlib \
	    CFLAGS="--sysroot=$SYSROOT -I$PREFIX/include $APP_CFLAGS" \
	    LDFLAGS="--sysroot=$SYSROOT -static -L$PREFIX/lib -L$SYSROOT/lib" \
	    >/tmp/$APP-conf.log 2>&1 ) || { tail -25 /tmp/$APP-conf.log; fail "configure failed"; }
fi

# Always remove the linked binary so `make` re-links it. The app executable does
# not depend on the external libphoenix.a / X11 .a archives, so a plain rebuild
# after one of those changes (e.g. a libphoenix fix) would otherwise SKIP relinking
# and ship a stale binary built against the old libs (the #58/mc "stale-binary
# trap"). Removing it forces the link; unchanged .o files are not recompiled.
rm -f "$XDIR/$APP"
if [ ! -x "$XDIR/$APP" ]; then
	echo "=== building $NV ==="
	# Keep xedit's bundled static libs (-lre -llisp -lmp, its regex + Lisp
	# interpreter) ahead of the X11 closure — overriding LDADD with only the X
	# libs drops them and the link fails on recomp/reexec/num_mode_infos.
	( cd "$XDIR" && make ${APP}_LDADD="-L. -lre -llisp -lmp $XCLOSURE" \
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
	for ad in Xedit Xedit-color; do
		[ -f "$XDIR/app-defaults/$ad" ] && cp "$XDIR/app-defaults/$ad" "$NFS/usr/share/X11/app-defaults/$ad"
	done
	echo "=== staged -> $NFS/bin/$APP (+ app-defaults/Xedit) ==="
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
echo "HW: in twm/wmaker (app-defaults REQUIRED):"
echo "    export XFILESEARCHPATH=/usr/share/X11/app-defaults/%N"
echo "    xedit /etc/hostname &"
