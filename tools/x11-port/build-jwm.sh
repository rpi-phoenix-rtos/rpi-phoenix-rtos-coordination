#!/usr/bin/env bash
#
# Phoenix-RTOS — build JWM (Joe's Window Manager) for aarch64-phoenix.
#
# JWM is the first taskbar/menu window manager ported to the Pi 4 X11 stack
# (Xphoenix kdrive fbdev DDX). It links statically against the X11 client/render
# lib stack already cross-compiled into $PREFIX by build-x11-phoenix.sh
# (libX11/libXext/libXpm/libXrender/libXmu + xcb + libXt/SM/ICE), so it needs no
# new libs of its own. Once running it shows a taskbar, a root menu and a managed
# (decorated) xeyes window — an unambiguous "real desktop" on the Pi's HDMI.
#
# SOURCE: JWM is built from the RELEASE TARBALL (jwm-<ver>.tar.xz), which ships a
# pre-generated ./configure — a git clone would need ./autogen.sh + autotools.
#
# Host-side only (does NOT boot the Pi, does NOT touch the flagship image).
# Idempotent: skips fetch/extract/build when the outputs already exist.
#
# Copyright 2026 Phoenix Systems
# Author: Witold Bołt
set -u

VER=2.4.6
NV=jwm-$VER
TARBALL=$NV.tar.xz
URL=https://github.com/joewing/jwm/releases/download/v$VER/$TARBALL

# Repo root derived from this script's own location (portable across checkouts).
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/../.." && pwd)"

TC=${ROOT}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-
SYSROOT=${ROOT}/.buildroot/_build/aarch64a72-generic-rpi4b/sysroot
PREFIX=/tmp/x11-phoenix
SRC=${ROOT}/tools/x11-port/src
JWMDIR=$SRC/$NV
ART=${ROOT}/artifacts/x11
NFS="${SHOWCASE_STAGE_DIR:-/srv/phoenix-rpi4-nfs}"

# The compiled-in SYSTEM config path. JWM's configure defines
# SYSTEM_CONFIG = "$sysconfdir/system.jwmrc" (NO extra jwm/ subdir), so to land
# the path at /etc/jwm/system.jwmrc we must pass --sysconfdir=/etc/jwm.
# netboot mounts the NFS export at /mnt (NOT /). This MUST match where the
# config is staged below — it is the #1 "WM starts but finds no config" failure.
SYSCONFDIR=/etc/jwm
EXPECT_CFG=$SYSCONFDIR/system.jwmrc

# Same X11-app build knobs as build_twm in build-x11-phoenix.sh.
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:$PREFIX/share/pkgconfig"
# CRITICAL: LIBDIR (not just PATH) so pkg-config CANNOT see host /usr/lib *.pc.
# Without this, JWM auto-detects host Xft/cairo/fontconfig and breaks the cross
# build. With it, only the prefix's .pc files are visible: x11/xext/xpm/xrender/
# xmu present (enabled), xft/cairo/jpeg/rsvg absent (auto-skipped).
export PKG_CONFIG_LIBDIR="$PREFIX/lib/pkgconfig:$PREFIX/share/pkgconfig"
PWD_DEFS="-DMAXHOSTNAMELEN=256 -DO_NOFOLLOW=0 -DXOS_USE_MTSAFE_PWDAPI -D_POSIX_THREAD_SAFE_FUNCTIONS=200809L"

# Static X11 link closure, correctly ORDERED (a lib must precede the lib that
# defines its symbols). Used both as configure LIBS (so each AC_CHECK_LIB probe
# links) and appended to the final link line (JWM's Makefile bakes its detected
# libs into LDFLAGS ending in -lXmu with no trailing X11 → Xmu's XDrawArcs etc.
# would be unresolved; appending the closure fixes the order).
XCLOSURE="-lXmu -lXt -lSM -lICE -lXpm -lXrender -lXext -lX11 -lxcb -lXau -lXdmcp"

fail() { echo "FAIL: $*"; exit 1; }

# --- 0. prerequisite: the X11 prefix must already be populated ---
if [ ! -f "$PREFIX/lib/libX11.a" ]; then
	echo "=== $PREFIX/lib/libX11.a missing — repopulating via build-x11-phoenix.sh ==="
	"$(dirname "$0")/build-x11-phoenix.sh" || fail "build-x11-phoenix.sh failed"
	[ -f "$PREFIX/lib/libX11.a" ] || fail "libX11.a still absent after repopulate"
fi

# --- 1. fetch + extract the RELEASE tarball (ships ./configure) ---
mkdir -p "$SRC"
if [ ! -d "$JWMDIR" ]; then
	if [ ! -f "$SRC/$TARBALL" ]; then
		echo "=== fetching $URL ==="
		curl -sSL -o "$SRC/$TARBALL" "$URL" || fail "download of $TARBALL failed (network egress?)"
	fi
	echo "=== extracting $TARBALL ==="
	tar -C "$SRC" -xf "$SRC/$TARBALL" || fail "extract of $TARBALL failed"
fi
[ -f "$JWMDIR/configure" ] || fail "$JWMDIR/configure missing — not a release tarball?"

# --- 1b. point JWM's hardcoded shell at the NFS export ---
# JWM runs EVERY command (StartupCommand, RootMenu <Program>, exec: actions) via
#   execl(SHELL_NAME, SHELL_NAME, "-c", command)
# and SHELL_NAME is a hardcoded "#define ... \"/bin/sh\"" in src/jwm.h. On the
# netboot Pi "/" is the RAM dummyfs root and the rootfs (incl. the shell) is at
# /mnt, so /bin/sh does NOT exist → nothing JWM launches would start (no
# xeyes window, dead menu) even though the WM itself runs. JWM does NOT consult
# $SHELL, so the env can't fix it; we redefine SHELL_NAME at compile time. Guard
# the header define with #ifndef (idempotent) so the -D below wins.
if ! grep -q '#ifndef SHELL_NAME' "$JWMDIR/src/jwm.h"; then
	perl -0pi -e 's{#define SHELL_NAME "/bin/sh"}{#ifndef SHELL_NAME\n#define SHELL_NAME "/bin/sh"\n#endif}' "$JWMDIR/src/jwm.h"
fi
SHELL_DEF='-DSHELL_NAME=\"/bin/sh\"'

# --- 2. configure (only if not already configured) ---
# --x-includes/--x-libraries point AC_PATH_X at the PREFIX so it does NOT probe
# and add host /usr/include (whose glibc headers break the cross compile).
# --disable-png/--disable-xinerama: there is no cross libpng/libXinerama in the
# prefix or sysroot, and JWM's non-pkg-config AC_CHECK_LIB fallback otherwise
# (mis)detects the HOST libpng → pulls -lpng16 onto the link line, which (a) is
# not cross-available and (b) poisons EVERY subsequent AC_CHECK_LIB probe. These
# are required-to-build disables, not feature trimming.
if [ ! -f "$JWMDIR/config.h" ]; then
	echo "=== configuring $NV (sysconfdir=$SYSCONFDIR) ==="
	( cd "$JWMDIR" && PKG_CONFIG="pkg-config --static" ./configure --host=aarch64-phoenix \
	    --prefix="$PREFIX" --sysconfdir="$SYSCONFDIR" \
	    --x-includes="$PREFIX/include" --x-libraries="$PREFIX/lib" \
	    --disable-png --disable-xinerama \
	    CC=${TC}gcc AR=${TC}ar RANLIB=${TC}ranlib \
	    CFLAGS="--sysroot=$SYSROOT -I$PREFIX/include $PWD_DEFS $SHELL_DEF" \
	    LDFLAGS="--sysroot=$SYSROOT -static -L$PREFIX/lib -L$SYSROOT/lib" \
	    LIBS="$XCLOSURE" >/tmp/jwm-conf.log 2>&1 ) || { tail -25 /tmp/jwm-conf.log; fail "configure failed"; }
fi

# Sanity-check what configure decided BEFORE building.
echo "=== configure feature summary ==="
grep -iE "X11:|XPM:|XRender:|Shape:|Xmu:|Xft:|Cairo:|PNG:|JPEG:|SVG:" /tmp/jwm-conf.log || true
gotpath=$(grep -h 'define SYSTEM_CONFIG' "$JWMDIR/config.h" | sed 's/.*"\(.*\)".*/\1/')
echo "compiled-in SYSTEM_CONFIG = $gotpath"
[ "$gotpath" = "$EXPECT_CFG" ] || fail "SYSTEM_CONFIG '$gotpath' != expected '$EXPECT_CFG'"

# --- 3. build (static); append the ordered closure to LDFLAGS for a clean link ---
if [ ! -x "$JWMDIR/src/jwm" ]; then
	echo "=== building $NV ==="
	( cd "$JWMDIR" && make \
	    LDFLAGS="--sysroot=$SYSROOT -static -L$PREFIX/lib -L$SYSROOT/lib $XCLOSURE" \
	    >/tmp/jwm-build.log 2>&1 ) || { tail -25 /tmp/jwm-build.log; fail "make failed"; }
fi
[ -x "$JWMDIR/src/jwm" ] || fail "src/jwm not produced"

# --- 4. publish + stage ---
mkdir -p "$ART"
cp "$JWMDIR/src/jwm" "$ART/jwm"
echo "=== published -> $ART/jwm ==="
if [ -d "$NFS/bin" ]; then
	cp "$JWMDIR/src/jwm" "$NFS/bin/jwm"
	chmod 755 "$NFS/bin/jwm"
	mkdir -p "$NFS/etc/jwm"
	# The config is tracked in the repo and hand-staged; only copy it onto the
	# export if a repo copy exists next to this script's tree (keeps the script
	# self-contained for a fresh export) — otherwise leave the existing one.
	REPO_CFG=${ROOT}/tools/x11-port/jwm/system.jwmrc
	if [ -f "$REPO_CFG" ]; then
		cp "$REPO_CFG" "$NFS/etc/jwm/system.jwmrc"
	fi
	echo "=== staged -> $NFS/bin/jwm + $NFS/etc/jwm/system.jwmrc ==="
	ls -l "$NFS/bin/jwm" "$NFS/etc/jwm/system.jwmrc" 2>&1
else
	echo "=== NFS export $NFS/bin not present — skipped staging (artifact only) ==="
fi

# --- 5. pre-flight checks (no Pi cycle) ---
echo "=== PRE-FLIGHT ==="
file "$ART/jwm"
echo "--- file must say: ELF 64-bit ... ARM aarch64 ... statically linked ---"
case "$(file "$ART/jwm")" in
	*"ARM aarch64"*"statically linked"*) echo "[OK] aarch64 static ELF" ;;
	*) fail "binary is not an aarch64 static ELF" ;;
esac

echo "--- undefined-symbol check (expect 0) ---"
und=$(${TC}nm -u "$ART/jwm" 2>/dev/null)
if [ -n "$und" ]; then echo "$und"; fail "undefined symbols present (not fully static)"; fi
echo "[OK] 0 undefined symbols"

echo "--- compiled-in config path (must be $EXPECT_CFG) ---"
cfgstr=$(strings "$NFS/bin/jwm" 2>/dev/null | grep -m1 "system.jwmrc")
echo "strings: $cfgstr"
if strings "$NFS/bin/jwm" 2>/dev/null | grep -q "^$EXPECT_CFG$"; then
	echo "[OK] compiled-in path == $EXPECT_CFG"
else
	fail "compiled-in jwmrc path does not match $EXPECT_CFG"
fi

echo "--- shell path JWM exec's its children with (must be on the export) ---"
if strings "$NFS/bin/jwm" 2>/dev/null | grep -q "^/bin/sh$"; then
	echo "[OK] SHELL_NAME == /bin/sh (StartupCommand + RootMenu programs will launch)"
else
	fail "SHELL_NAME is not /bin/sh — JWM-launched commands (xeyes, menu) would fail"
fi

if [ -f "$NFS/etc/jwm/system.jwmrc" ]; then
	echo "[OK] config present at $NFS/etc/jwm/system.jwmrc (Pi: $EXPECT_CFG)"
else
	echo "[WARN] $NFS/etc/jwm/system.jwmrc not present — stage it before booting"
fi

echo "=== ALL PRE-FLIGHT CHECKS PASSED ==="
echo "HW test (orchestrator): boot netboot image, then:"
echo "    ls /bin            # #156 warmup"
echo "    /bin/startx jwm    # expect taskbar + menu + decorated xeyes"
