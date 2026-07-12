#!/usr/bin/env bash
#
# Phoenix-RTOS — fetch, configure and build the xorg-server 1.20.14 kdrive CORE
# archives for aarch64-phoenix.
#
# This is the step that build-xfbdev.sh depends on: it links Xphoenix out of the
# xorg-server core archives (dix/os/mi/fb/record/...), but it does NOT build them
# — it assumes the tree is already configured + compiled. Historically that was a
# MANUAL step (documented in PROGRESS.md), so on the original dev host the tree
# existed but on a fresh clone it never did, and the Xphoenix build failed at
# `make -C record` ("RECORD rebuild FAIL"). This script automates that step so a
# clean machine reproduces the host, using the exact configure invocation the dev
# host recorded in its config.log (both drop USE_TERMINFO etc. via flags).
#
# The X client/render/font libraries the server links against must already be
# built into $PREFIX by build-x11-phoenix.sh (run first, as the orchestrator does).
#
# Host-side only. Does NOT boot the Pi, does NOT touch the flagship image.
# Idempotent: skips fetch/extract/configure/make when the core archives exist.
#
# Copyright 2026 Phoenix Systems
# Author: Witold Bołt
set -u

# Repo root derived from this script's own location (portable across checkouts).
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/../.." && pwd)"

TC=${ROOT}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-
SYSROOT=${ROOT}/.buildroot/_build/aarch64a72-generic-rpi4b/sysroot
PREFIX=/tmp/x11-phoenix
SRC=${ROOT}/tools/x11-port/src
VER=1.20.14
NV=xorg-server-$VER
KD=$SRC/$NV
# x.org individual release; the www.x.org URL 301-redirects to xorg.freedesktop.org
# (curl -L follows), same pattern as the other X tarballs in build-x11-phoenix.sh.
URL=https://www.x.org/releases/individual/xserver/$NV.tar.gz

fail() { echo "build-xserver-core: FATAL: $*" >&2; exit 1; }

# The 25 core archives build-xfbdev.sh links (mirrors its core_la[] list). If all
# are present the core is already built and there is nothing to do.
core_archives=(
  dix/.libs/libmain.a dix/.libs/libdix.a hw/kdrive/src/.libs/libkdrive.a
  fb/.libs/libfb.a mi/.libs/libmi.a xfixes/.libs/libxfixes.a
  Xext/.libs/libXext.a Xext/.libs/libXvidmode.a Xext/.libs/libhashtable.a
  dbe/.libs/libdbe.a record/.libs/librecord.a randr/.libs/librandr.a
  render/.libs/librender.a damageext/.libs/libdamageext.a present/.libs/libpresent.a
  miext/sync/.libs/libsync.a miext/damage/.libs/libdamage.a miext/shadow/.libs/libshadow.a
  Xi/.libs/libXi.a Xi/.libs/libXistubs.a xkb/.libs/libxkb.a xkb/.libs/libxkbstubs.a
  composite/.libs/libcomposite.a config/.libs/libconfig.a os/.libs/libos.a
)
all_present() {
  local a
  for a in "${core_archives[@]}"; do
    [ -f "$KD/$a" ] || return 1
  done
  return 0
}

# libmd (SHA1) must exist in $PREFIX independently of the core-archive cache: it
# lives under $PREFIX (/tmp/x11-phoenix), which can be cleared on its own (e.g. a
# /tmp cleanup), while the core archives survive. build-xfbdev.sh links -lmd from
# $PREFIX, so ensure libmd BEFORE the all_present early-return below — otherwise a
# cached-archive run leaves no libmd and the Xphoenix relink fails ("cannot find
# -lmd").
if [ ! -f "$PREFIX/lib/libmd.a" ]; then
  echo "=== building libmd (SHA1) into $PREFIX ==="
  X11_PREFIX="$PREFIX" SYSROOT="$SYSROOT" TOOLCHAIN_BIN="${TC%/aarch64-phoenix-}" \
    "$ROOT/tools/x11-port/libmd-phoenix/build.sh" >/tmp/libmd-build.log 2>&1 \
    || { cat /tmp/libmd-build.log; fail "libmd build failed"; }
fi

if all_present; then
  echo "=== xorg-server $VER core archives already built — skipping ==="
  exit 0
fi

[ -x "${TC}gcc" ] || fail "toolchain missing: ${TC}gcc (build the toolchain first)"
[ -d "$SYSROOT/lib" ] || fail "sysroot missing: $SYSROOT (run rebuild-rpi4b-fast.sh first)"
[ -d "$PREFIX/lib/pkgconfig" ] || fail "X libs not in $PREFIX — run build-x11-phoenix.sh first"

# --- 1. fetch + extract the release tarball (ships a generated ./configure) ---
mkdir -p "$SRC"
if [ ! -f "$KD/configure" ]; then
  echo "=== fetching $NV ==="
  ( cd "$SRC" && timeout 180 curl -sSL -o "$NV.tar.gz" "$URL" ) || fail "fetch failed: $URL"
  ( cd "$SRC" && tar xf "$NV.tar.gz" ) || fail "extract failed: $NV.tar.gz"
  [ -f "$KD/configure" ] || fail "$KD/configure missing after extract"
fi

# --- 1b. libmd (SHA1) is built above, before the all_present early-return, so a
# cached-archive run still leaves a linkable libmd in $PREFIX for build-xfbdev.sh.

# --- 2. configure (exact invocation the dev host recorded; kdrive core only) ---
# The disable-* flags keep this to the kdrive core; --with-sha1=libmd avoids
# openssl, --disable-xdmcp skips the ifa_broadaddr path libphoenix lacks, and the
# -D CFLAGS cover the remaining Phoenix os-layer gaps (SI_USER, O_NOFOLLOW, ...).
# PKG_CONFIG_LIBDIR pins pkg-config to $PREFIX only, so the cross configure never
# sees the build machine's /usr/lib *.pc (hermetic — the reproducibility fix).
# Both dirs are required: the X *libraries* (pixman/xfont2/xcb) install their .pc
# under lib/pkgconfig, but the X *protocol* packages (xorgproto: xproto/fixesproto/
# damageproto/...) install under share/pkgconfig — omitting share/ makes configure
# fail "Package requirements ... were not met".
if [ ! -f "$KD/config.status" ]; then
  echo "=== configuring $NV (kdrive core, aarch64-phoenix) ==="
  ( cd "$KD" && \
    PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:$PREFIX/share/pkgconfig" \
    PKG_CONFIG_LIBDIR="$PREFIX/lib/pkgconfig:$PREFIX/share/pkgconfig" \
    ./configure --host=aarch64-phoenix --prefix="$PREFIX" \
      --enable-kdrive --disable-xephyr --with-sha1=libmd \
      --disable-xorg --disable-xwayland --disable-xnest --disable-xvfb --disable-dmx \
      --disable-glamor --disable-dri --disable-dri2 --disable-dri3 --disable-glx \
      --disable-int10-module --disable-vgahw --disable-vbe --disable-xdmcp \
      --disable-xinerama --without-dtrace --disable-systemd-logind --disable-secure-rpc \
      --disable-config-udev --disable-config-hal --without-systemd-daemon --disable-unit-tests \
      CC=${TC}gcc AR=${TC}ar RANLIB=${TC}ranlib \
      CFLAGS="--sysroot=$SYSROOT -I$PREFIX/include -DMAXHOSTNAMELEN=256 -DXOS_USE_MTSAFE_PWDAPI -D_POSIX_THREAD_SAFE_FUNCTIONS=200809L -DO_NOFOLLOW=0 -DSI_USER=0" \
      LDFLAGS="--sysroot=$SYSROOT -L$PREFIX/lib -L$SYSROOT/lib" \
      >/tmp/$NV-conf.log 2>&1 ) || { tail -30 /tmp/$NV-conf.log; fail "configure failed (see /tmp/$NV-conf.log)"; }
fi

# --- 3. build the core archives ---
# --disable-xephyr means no server binary is linked here (Xphoenix is linked by
# build-xfbdev.sh from these archives), so `make` only compiles the libs. It can
# still exit non-zero on a trailing no-op target; the archive presence check below
# is the authoritative success gate.
echo "=== building $NV core (make -j$(nproc)) ==="
( cd "$KD" && make -j"$(nproc)" >/tmp/$NV-build.log 2>&1 ) \
  || echo "=== make returned non-zero — verifying archives directly ==="
all_present || { tail -40 /tmp/$NV-build.log; fail "core archives still missing after make (see /tmp/$NV-build.log)"; }

echo "=== OK: xorg-server $VER core archives built for aarch64-phoenix ==="
