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
PATCHDIR=$TOOLS/patches

# xedit's bundled Lisp interpreter loads its module .lsp files from a compiled-in
# LISPDIR via (require "lisp") during LispBegin(). LISPDIR must be a Pi-resident
# path (NOT the host build prefix /tmp/x11-phoenix), and the .lsp tree must be
# staged there, or (require "lisp") fails and the interpreter Data-Aborts at
# startup (NULL *PACKAGE* -> NULL package deref in LispProclaimSpecial). See
# patches/xedit-1.2.2-phoenix-lispbegin-savepackage-null.patch and
# docs/inprogress/2026-06-29-xedit-lisp-port.md.
LISPDIR_PI=/usr/lib/X11/xedit/lisp

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

# Apply Phoenix patches (idempotent, -N). Currently only the LispBegin
# savepackage-NULL guard for the bundled Lisp interpreter.
for p in "$PATCHDIR/$NV"*.patch; do
	[ -f "$p" ] && ( cd "$XDIR" && patch -p1 -N <"$p" >/dev/null 2>&1 )
done

# xedit-1.2.2 ships a 2014 config.sub that predates the "phoenix" OS triplet.
# Refresh it (+ config.guess) from a newer xorg app tree so --host=aarch64-phoenix
# is accepted.
for cfg in config.sub config.guess; do
	if ! grep -q phoenix "$XDIR/$cfg" 2>/dev/null; then
		src=$(grep -lr phoenix "$SRC"/*/$cfg 2>/dev/null | head -1)
		[ -n "$src" ] && cp "$src" "$XDIR/$cfg" && echo "=== refreshed $cfg (phoenix-aware) ==="
	fi
done

# Reconfigure if config.status is missing OR was generated with a different
# (stale) LISPDIR. The LISPDIR is baked into the binary via -DLISPDIR, so a
# stale config.status would silently ship a binary pointing at the wrong path.
need_configure=1
if [ -f "$XDIR/config.status" ] && grep -q "LISPDIR.*$LISPDIR_PI" "$XDIR/Makefile" 2>/dev/null; then
	need_configure=0
fi
if [ "$need_configure" = 1 ]; then
	echo "=== configuring $NV (LISPDIR=$LISPDIR_PI) ==="
	( cd "$XDIR" && ./configure --host=aarch64-phoenix --prefix="$PREFIX" \
	    --with-lispdir="$LISPDIR_PI" \
	    CC=${TC}gcc AR=${TC}ar RANLIB=${TC}ranlib \
	    CFLAGS="--sysroot=$SYSROOT -I$PREFIX/include $APP_CFLAGS" \
	    LDFLAGS="--sysroot=$SYSROOT -static -L$PREFIX/lib -L$SYSROOT/lib" \
	    >/tmp/$APP-conf.log 2>&1 ) || { tail -25 /tmp/$APP-conf.log; fail "configure failed"; }
	# Force a recompile of the Lisp interpreter so the new -DLISPDIR takes
	# effect (configure alone does not invalidate the cached .o files).
	rm -f "$XDIR"/lisp/*.o "$XDIR/liblisp.a"
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
	# Stage the Lisp interpreter's module files at the compiled-in LISPDIR so
	# (require "lisp") succeeds at startup (see header). Mirror modules/ +
	# modules/progmodes/ exactly; require.c builds LISPDIR + "/" + name + ".lsp".
	LSPSRC="$XDIR/lisp/modules"
	LSPDST="$NFS$LISPDIR_PI"
	mkdir -p "$LSPDST/progmodes"
	cp "$LSPSRC"/*.lsp "$LSPDST/" 2>/dev/null
	cp "$LSPSRC"/progmodes/*.lsp "$LSPDST/progmodes/" 2>/dev/null
	nlsp=$(ls "$LSPDST"/*.lsp "$LSPDST"/progmodes/*.lsp 2>/dev/null | wc -l)
	echo "=== staged -> $NFS/bin/$APP (+ app-defaults/Xedit + $nlsp lisp module .lsp under $LISPDIR_PI) ==="
fi

echo "=== PRE-FLIGHT ==="
file "$ART/$APP"
case "$(file "$ART/$APP")" in
	*"ARM aarch64"*"statically linked"*) echo "[OK] aarch64 static ELF" ;;
	*) fail "binary is not an aarch64 static ELF" ;;
esac
und=$(${TC}nm -u "$ART/$APP" 2>/dev/null)
[ -z "$und" ] && echo "[OK] 0 undefined symbols" || { echo "$und"; fail "undefined symbols present"; }
# The Lisp interpreter must carry the Pi-resident LISPDIR, not the host prefix.
if strings "$ART/$APP" | grep -q "$LISPDIR_PI"; then
	echo "[OK] LISPDIR baked: $LISPDIR_PI"
else
	fail "binary does not contain LISPDIR=$LISPDIR_PI (stale -DLISPDIR?)"
fi
strings "$ART/$APP" | grep -q "/tmp/x11-phoenix/lib/X11/xedit/lisp" \
	&& echo "[WARN] stale /tmp/x11-phoenix LISPDIR also present in binary"
echo "=== ALL PRE-FLIGHT CHECKS PASSED ==="
echo "HW: in twm/wmaker (app-defaults REQUIRED):"
echo "    export XFILESEARCHPATH=/usr/share/X11/app-defaults/%N"
echo "    xedit /etc/hostname &"
