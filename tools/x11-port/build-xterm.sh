#!/usr/bin/env bash
#
# Phoenix-RTOS — build xterm (the X terminal emulator) for aarch64-phoenix.
#
# xterm is the first TERMINAL EMULATOR ported to the Pi 4 X11 stack (Xphoenix
# kdrive fbdev DDX). It forks a shell on a Phoenix pty (/dev/ptmx + /dev/pts/N,
# served by posixsrv) and renders it in an X window, so it validates the full
# keyboard path HID -> usbkbd -> /dev/kbd0 -> X server -> xterm -> shell (task
# #30) — an unambiguous interactive demo. It links statically against the X11
# client/toolkit lib stack already cross-compiled into $PREFIX by
# build-x11-phoenix.sh (libXaw/Xmu/Xt/Xext/Xpm/Xrender/X11 + xcb).
#
# SOURCE: the xterm snapshot RELEASE tarball (xterm-snapshots-xterm-<ver>),
# which ships a pre-generated ./configure (a git clone would need autoconf).
#
# Host-side only (does NOT boot the Pi, does NOT touch the flagship image).
# Idempotent: skips fetch/extract/patch/build when the outputs already exist.
#
# Copyright 2026 Phoenix Systems
# Author: Witold Bołt
set -u

VER=396
NV=xterm-$VER
TARBALL=$NV.tar.gz
# The github snapshots mirror ships a generated ./configure under the dir name
# xterm-snapshots-xterm-<ver>; we extract + rename to xterm-<ver>.
URL=https://github.com/ThomasDickey/xterm-snapshots/archive/refs/tags/xterm-$VER.tar.gz
UPSTREAM_DIR=xterm-snapshots-xterm-$VER

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

# Persistent (committed) port files live under tools/x11-port/xterm/ because the
# extracted source tree (tools/x11-port/src/) is .gitignored — a fresh extract
# must be able to recover the patch, the shims and the termcap stub from here.
PORTDIR=$TOOLS/xterm
PATCHFILE=$TOOLS/patches/xterm-$VER-phoenix.patch
FDSET_SHIM=$PORTDIR/xterm-phoenix-fdset-shim.h
WCTYPE_INC=$PORTDIR/include

# The compile-time fallback shell. On the netboot Pi "/" is the RAM dummyfs root
# and the rootfs (incl. the shell) lives at /mnt, so "/bin/sh" does not
# exist. xterm resolves its shell as -e program, then $SHELL, then passwd
# pw_shell, then this compiled default; xlaunch sets neither $SHELL nor
# pw_shell, so THIS is what xterm exec's. (Patched resetShell() guards it with
# #ifndef so this -D wins.) Same lesson as JWM's SHELL_NAME.
SHELL_PATH=/bin/sh

# pkg-config: prefix .pc only (LIBDIR, not just PATH) so host /usr/lib *.pc are
# invisible to the cross configure — same precaution as build-jwm.sh.
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:$PREFIX/share/pkgconfig"
export PKG_CONFIG_LIBDIR="$PREFIX/lib/pkgconfig:$PREFIX/share/pkgconfig"

# Static X11 link closure, correctly ORDERED, appended AFTER the objects via
# EXTRA_LOADFLAGS (the Makefile's xterm rule is `... $(OBJS1) $(LIBS)
# $(EXTRA_LOADFLAGS)`, so libs placed here follow the objects that reference
# them). phoenix_termcap.o (the no-curses termcap stub) is included; -lphoenix
# -lc close out wcslen and friends. Putting the closure in LDFLAGS instead
# would place it BEFORE the objects → unresolved X symbols.
XCLOSURE="-lXaw7 -lXmu -lXt -lSM -lICE -lXpm -lXrender -lXext -lX11 -lxcb -lXau -lXdmcp -lphoenix -lc"

# Force-include the fd_set shim (fd_mask + __fds_bits alias for Xpoll.h) and add
# the wctype.h shim dir ahead of the prefix includes. P_tmpdir + the shell
# default complete the -D set.
SHIM_DEFS="-include $FDSET_SHIM -I$WCTYPE_INC"
APP_DEFS="-DDEFSHELL_NAME=\\\"$SHELL_PATH\\\" -DP_tmpdir=\\\"/tmp\\\""

fail() { echo "FAIL: $*"; exit 1; }

# --- 0. prerequisite: the X11 prefix must already be populated ---
if [ ! -f "$PREFIX/lib/libX11.a" ]; then
	echo "=== $PREFIX/lib/libX11.a missing — repopulating via build-x11-phoenix.sh ==="
	"$TOOLS/build-x11-phoenix.sh" || fail "build-x11-phoenix.sh failed"
	[ -f "$PREFIX/lib/libX11.a" ] || fail "libX11.a still absent after repopulate"
fi

# --- 1. fetch + extract the RELEASE tarball (ships ./configure) ---
mkdir -p "$SRC"
if [ ! -d "$XDIR" ]; then
	if [ ! -f "$SRC/$TARBALL" ]; then
		echo "=== fetching $URL ==="
		curl -sSL -o "$SRC/$TARBALL" "$URL" || fail "download of $TARBALL failed (network egress?)"
	fi
	echo "=== extracting $TARBALL ==="
	tar -C "$SRC" -xf "$SRC/$TARBALL" || fail "extract of $TARBALL failed"
	[ -d "$SRC/$UPSTREAM_DIR" ] || fail "expected $SRC/$UPSTREAM_DIR after extract"
	mv "$SRC/$UPSTREAM_DIR" "$XDIR"
fi
[ -f "$XDIR/configure" ] || fail "$XDIR/configure missing — not a release tarball?"

# --- 1b. apply the Phoenix source patch (idempotent) ---
# Adds: get_pty() /dev/ptmx branch (__phoenix__), USE_POSIX_TERMIOS for
# __phoenix__ (so <termios.h> + struct winsize are included), USE_SYSV_PGRP for
# __phoenix__ (POSIX setsid()/setpgrp(void), not the 2-arg BSD form), a
# DEFSHELL_NAME fallback in resetShell(), and a __phoenix__ branch in
# xtermcap.h that uses the local termcap stub instead of <curses.h>.
# The patch only touches main.c/xterm_io.h/xtermcap.h; the stub + shim files are
# copied in separately (step 1c). Use a marker to make the apply idempotent.
if ! grep -q '__phoenix__' "$XDIR/xterm_io.h"; then
	echo "=== applying $PATCHFILE ==="
	( cd "$XDIR" && patch -p1 < "$PATCHFILE" ) || fail "patch apply failed"
fi
grep -q '__phoenix__' "$XDIR/xterm_io.h" || fail "patch did not land (no __phoenix__ in xterm_io.h)"

# --- 1c. drop in the no-curses termcap stub source (compiled into xterm) ---
# Phoenix has no curses/termcap library; phoenix_termcap.[ch] provide tgetent()
# -> "no entry" stubs so xterm links and degrades gracefully (built-in keysym
# tables drive the keyboard; only the cosmetic $TERMCAP sync is skipped). The
# .c is built to a .o and appended to the link via XCLOSURE.
cp "$PORTDIR/phoenix_termcap.c" "$XDIR/phoenix_termcap.c" || fail "phoenix_termcap.c missing in $PORTDIR"
cp "$PORTDIR/phoenix_termcap.h" "$XDIR/phoenix_termcap.h" || fail "phoenix_termcap.h missing in $PORTDIR"

# --- 2. configure (only if not already configured) ---
# --disable-freetype: CORE X bitmap fonts only (no Xft/fontconfig in this stack;
#   the server serves misc fonts via -fp). --disable-luit/--disable-imake/
#   --without-utempter/--without-xpm-app trim deps Phoenix lacks. --disable-
#   tcap-fkeys/--disable-tcap-query drop the termcap function-key features
#   (curses-dependent); the stub covers what remains.
# --x-includes/--x-libraries point AC_PATH_X at the PREFIX (no host /usr probe).
if [ ! -f "$XDIR/config.status" ]; then
	echo "=== configuring $NV ==="
	( cd "$XDIR" && PKG_CONFIG="pkg-config --static" ./configure --host=aarch64-phoenix \
	    --prefix="$PREFIX" \
	    --x-includes="$PREFIX/include" --x-libraries="$PREFIX/lib" \
	    --disable-freetype --disable-luit --disable-imake --without-utempter \
	    --disable-toolbar --disable-double-buffer --disable-session-mgt \
	    --without-xpm --disable-tcap-fkeys --disable-tcap-query \
	    CC=${TC}gcc AR=${TC}ar RANLIB=${TC}ranlib \
	    CFLAGS="--sysroot=$SYSROOT -I$PREFIX/include" \
	    LDFLAGS="--sysroot=$SYSROOT -static -L$PREFIX/lib -L$SYSROOT/lib" \
	    >/tmp/xterm-conf.log 2>&1 ) || { tail -25 /tmp/xterm-conf.log; fail "configure failed"; }
fi

# --- 3. build the termcap stub object + the xterm target (static) ---
if [ ! -x "$XDIR/xterm" ]; then
	echo "=== building $NV (xterm target only; the resize util is not staged) ==="
	${TC}gcc --sysroot=$SYSROOT -I"$PREFIX/include" \
	    -c "$XDIR/phoenix_termcap.c" -o "$XDIR/phoenix_termcap.o" \
	    || fail "phoenix_termcap.o build failed"
	( cd "$XDIR" && make xterm \
	    CFLAGS="--sysroot=$SYSROOT $SHIM_DEFS -I$PREFIX/include $APP_DEFS" \
	    LDFLAGS="--sysroot=$SYSROOT -static -L$PREFIX/lib -L$SYSROOT/lib" \
	    EXTRA_LOADFLAGS="$XDIR/phoenix_termcap.o $XCLOSURE" \
	    >/tmp/xterm-build.log 2>&1 ) || { tail -30 /tmp/xterm-build.log; fail "make xterm failed"; }
fi
[ -x "$XDIR/xterm" ] || fail "xterm not produced"

# --- 4. publish + stage ---
mkdir -p "$ART"
cp "$XDIR/xterm" "$ART/xterm"
echo "=== published -> $ART/xterm ==="
if [ -d "$NFS/bin" ]; then
	cp "$XDIR/xterm" "$NFS/bin/xterm"
	chmod 755 "$NFS/bin/xterm"
	# Minimal terminfo (xterm sets TERM=xterm for the child; busybox ash uses
	# hardcoded ANSI and ignores terminfo, but stage xterm/vt100 entries for any
	# terminfo-aware program run inside). Maps to /usr/share/terminfo.
	mkdir -p "$NFS/usr/share/terminfo/x" "$NFS/usr/share/terminfo/v"
	for ti in x/xterm x/xterm-256color v/vt100; do
		[ -f "/usr/share/terminfo/$ti" ] && cp "/usr/share/terminfo/$ti" "$NFS/usr/share/terminfo/$ti"
	done
	echo "=== staged -> $NFS/bin/xterm (+ terminfo under $NFS/usr/share/terminfo) ==="
	ls -l "$NFS/bin/xterm"
else
	echo "=== NFS export $NFS/bin not present — skipped staging (artifact only) ==="
fi

# --- 5. pre-flight checks (no Pi cycle) ---
echo "=== PRE-FLIGHT ==="
file "$ART/xterm"
case "$(file "$ART/xterm")" in
	*"ARM aarch64"*"statically linked"*) echo "[OK] aarch64 static ELF" ;;
	*) fail "binary is not an aarch64 static ELF" ;;
esac

echo "--- undefined-symbol check (expect 0) ---"
und=$(${TC}nm -u "$ART/xterm" 2>/dev/null)
if [ -n "$und" ]; then echo "$und"; fail "undefined symbols present (not fully static)"; fi
echo "[OK] 0 undefined symbols"

echo "--- pty multiplexor path (must open /dev/ptmx) ---"
strings "$ART/xterm" | grep -qx "/dev/ptmx" && echo "[OK] /dev/ptmx present (SVR4 pty open)" \
	|| fail "/dev/ptmx not in binary — get_pty() __phoenix__ branch missing?"

echo "--- compiled-in fallback shell (must be on the export) ---"
strings "$ART/xterm" | grep -qx "$SHELL_PATH" && echo "[OK] DEFSHELL_NAME == $SHELL_PATH" \
	|| fail "fallback shell is not $SHELL_PATH — xterm's forked shell would not start"

echo "=== ALL PRE-FLIGHT CHECKS PASSED ==="
echo "HW test (orchestrator): boot netboot image, then:"
echo "    ls /bin                 # #156 warmup"
echo "    /bin/startx term        # twm (focus) + xterm (managed terminal)"
echo "  In the xterm window: type 'ls /bin' + Enter — output validates HID->X->pty->shell."
