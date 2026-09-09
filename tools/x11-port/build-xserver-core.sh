#!/usr/bin/env bash
#
# Phoenix-RTOS — fetch, configure and build the xorg-server 21.1.24 kdrive CORE
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
VER=21.1.24
NV=xorg-server-$VER
KD=$SRC/$NV
# x.org individual release; the www.x.org URL 301-redirects to xorg.freedesktop.org
# (curl -L follows), same pattern as the other X tarballs in build-x11-phoenix.sh.
URL=https://www.x.org/releases/individual/xserver/$NV.tar.gz

# --glamor (E5 / G-XORG-MODERN): additionally build glamor (libglamor.a) for the
# GPU-accelerated Xphoenix-glamor server (build-xfbdev.sh --glamor links it).
# Reconfigures with --enable-glamor + the epoxy-shim GLAMOR_CFLAGS override —
# autoconf skips the PKG_CHECK_MODULES([GLAMOR],[epoxy]) query when *_CFLAGS and
# *_LIBS are pre-set, so no epoxy.pc is needed (see tools/x11-port/glamor-shim/).
# Default (no flag) builds the software-only core EXACTLY as before. This makes the
# whole glamor build chain reproducible from a clean tree (retires the ad-hoc
# --enable-glamor reconfigure the M0/M1 bring-up left behind).
GLAMOR=0
[ "${1:-}" = "--glamor" ] && GLAMOR=1
GLAMOR_SHIM="$ROOT/tools/x11-port/glamor-shim"
GLAMOR_MESA_GL="$ROOT/external/mesa/include"
GLAMOR_A="$KD/glamor/.libs/libglamor.a"
# Records the glamor state the tree was last configured in; a mismatch vs the
# requested state forces a reconfigure (the config.status skip would keep it stale).
GLAMOR_MARK="$KD/.phoenix-glamor-enabled"
glamor_marker_matches() { if [ "$GLAMOR" = 1 ]; then [ -f "$GLAMOR_MARK" ]; else [ ! -f "$GLAMOR_MARK" ]; fi; }

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
# Durable glamor core-source fixes (each patch header explains itself). Applied
# on BOTH paths -- the slow full-build path and the "already built" early return
# -- because the archives must be rebuilt for a fix to reach the link. patch -N is
# idempotent.
#
# When adding a patch here, name it xorg-server-$VER-glamor-*.patch and append it
# to this list; do NOT add a second apply function (the early-return path would
# then need updating twice, which is how the chain fix was silently skipped for
# a while).
#
# ⚠️  A patch that lands must force a FULL rebuild, not a rebuild of libglamor
# alone. This used to `make -C $KD/glamor` and return, leaving one freshly
# compiled archive linked against xorg core archives from an earlier build. That
# is a partial rebuild, and on 2026-09-08 it produced an X server that started,
# brought up all its clients, and then died with SIGILL -- which cost a wrong
# root-cause (the crash was blamed on a Mesa bump that turned out to be innocent;
# see docs/misc/2026-09-08-glamor-screen-mirror-and-gl-window-rate.md §8).
# Rebuilding one archive is faster and occasionally wrong, which is the worst
# trade available here.
glamor_core_patches=(
  glamor-rgba-upload
  glamor-destroypixmap-chain
)
# Patches that were applied in the past and must now be REVERSED out of an
# existing build tree (a fresh extract simply never gets them). Both were
# compensators for Mesa's size-based scanout Y-flip, which the glamor shim now
# opts out of (phx_scanout_flip_gate = 0), so the flip they corrected is gone:
#   - screen-upload-yflip: wrote screen-pixmap uploads AND downloads Y-mirrored.
#   - screen-upload-bulk:  made that mirrored upload one glTexSubImage2D instead
#     of one per row. Reverting it keeps the speed -- upstream's unflipped path
#     is a single bulk glTexSubImage2D already.
reverted_core_patches=(
  glamor-screen-upload-bulk
  glamor-screen-upload-yflip
)
# Patches to os/ and other backend-independent core code. Applied for every
# build, glamor or not.
os_core_patches=(
  os-client-rcvbuf
)
# Set to 1 by apply_core_patches when a patch actually landed, so the
# "already built" early return below is skipped and the full build runs.
GLAMOR_PATCH_LANDED=0
revert_core_patches() {
  local name pf
  for name in "${reverted_core_patches[@]}"; do
    pf="$ROOT/tools/x11-port/patches/xorg-server-${VER}-${name}.patch"
    [ -f "$pf" ] || continue
    if patch -d "$KD" -p1 -R -N --dry-run <"$pf" >/dev/null 2>&1; then
      echo "=== reverting retired core patch: $name ==="
      patch -d "$KD" -p1 -R -N <"$pf" >/dev/null 2>&1 || true
      GLAMOR_PATCH_LANDED=1
    fi
  done
}

apply_core_patches() {
  local name pf list=("${os_core_patches[@]}")
  [ "$GLAMOR" = 1 ] && list+=("${glamor_core_patches[@]}")
  for name in "${list[@]}"; do
    pf="$ROOT/tools/x11-port/patches/xorg-server-${VER}-${name}.patch"
    [ -f "$pf" ] || continue
    if patch -d "$KD" -p1 -N --dry-run <"$pf" >/dev/null 2>&1; then
      echo "=== applying core patch: $name ==="
      patch -d "$KD" -p1 -N <"$pf" >/dev/null 2>&1 || true
      GLAMOR_PATCH_LANDED=1
    fi
  done
  if [ "$GLAMOR_PATCH_LANDED" = 1 ]; then
    echo "=== a core patch landed — forcing a FULL core rebuild ==="
    echo "=== (rebuilding libglamor alone links it against stale siblings) ==="
  fi
}

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

# Already-built = software archives present AND the configured glamor state matches
# the request AND (for --glamor) libglamor.a is present.
# Any glamor source newer than the built archive means the archive is stale --
# whoever changed it (a patch from the list, or a hand edit while iterating on a
# diagnostic) will otherwise get the OLD binary with no warning. This has now
# bitten twice: the second time, a probe was added to glamor_copy.c, the patch
# was already applied so `patch --dry-run` failed, GLAMOR_PATCH_LANDED stayed 0,
# and the fast path shipped a binary with none of the probe in it -- the tell
# being `strings | grep -c` returning 0 on a freshly dated file.
# Third recurrence of the same footgun, so the guard now covers every directory
# a fix can land in, not just glamor/: an os/connection.c change was invisible to
# a glamor-only check and would have shipped an unpatched binary again.
stale_dirs=("glamor:$GLAMOR_A" "os:$KD/os/.libs/libos.a" "dix:$KD/dix/.libs/libdix.a"
            "hw/kdrive/src:$KD/hw/kdrive/src/.libs/libkdrive.a")
core_src_newer_than_archive() {
  local ent dir arch
  for ent in "${stale_dirs[@]}"; do
    dir="$KD/${ent%%:*}"; arch="${ent#*:}"
    [ "$dir" = "$KD/glamor" ] && [ "$GLAMOR" != 1 ] && continue
    [ -d "$dir" ] || continue
    [ -f "$arch" ] || return 0
    if [ -n "$(find "$dir" -maxdepth 1 \( -name '*.c' -o -name '*.h' \) \
               -newer "$arch" -print -quit 2>/dev/null)" ]; then
      echo "=== $dir source is newer than $(basename "$arch") — rebuilding ==="
      return 0
    fi
  done
  return 1
}

core_built() {
  # A patch that just landed invalidates every archive, not only libglamor.
  [ "$GLAMOR_PATCH_LANDED" = 0 ] || return 1
  core_src_newer_than_archive && return 1
  all_present || return 1
  glamor_marker_matches || return 1
  [ "$GLAMOR" = 0 ] || [ -f "$GLAMOR_A" ]
}
# Apply the durable core-source patches BEFORE deciding whether the tree is
# already built. Two ordering mistakes have been made here, in opposite
# directions, so both are spelled out: the apply used to sit BELOW the early
# return, which silently skipped any newly added patch on every run after the
# first successful build; and it then sat INSIDE the early-return branch, where
# it could rebuild libglamor but could no longer influence the already-evaluated
# core_built decision. It has to run first, and core_built has to consult its
# result.
revert_core_patches
apply_core_patches

if core_built; then
  echo "=== xorg-server $VER core archives already built (glamor=$GLAMOR) — skipping ==="
  exit 0
fi

# Requested glamor state differs from how the tree is configured → force a
# reconfigure (else the config.status check below keeps the stale glamor setting).
if [ -f "$KD/config.status" ] && ! glamor_marker_matches; then
  echo "=== glamor state change (want glamor=$GLAMOR) — forcing reconfigure ==="
  rm -f "$KD/config.status"
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
  glamor_arg="--disable-glamor"
  if [ "$GLAMOR" = 1 ]; then
    glamor_arg="--enable-glamor"
    # Feed the epoxy shim + Mesa GL headers to PKG_CHECK_MODULES([GLAMOR],[epoxy])
    # via the env (autoconf skips the pkg-config query when both are pre-set).
    export GLAMOR_CFLAGS="-I$GLAMOR_SHIM -I$GLAMOR_MESA_GL"
    export GLAMOR_LIBS=" "
  fi
  echo "=== configuring $NV (kdrive core, aarch64-phoenix, glamor=$GLAMOR) ==="
  ( cd "$KD" && \
    PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:$PREFIX/share/pkgconfig" \
    PKG_CONFIG_LIBDIR="$PREFIX/lib/pkgconfig:$PREFIX/share/pkgconfig" \
    ./configure --host=aarch64-phoenix --prefix="$PREFIX" \
      --enable-kdrive --disable-xephyr --with-sha1=libmd \
      --disable-xorg --disable-xwayland --disable-xnest --disable-xvfb --disable-dmx \
      $glamor_arg --disable-dri --disable-dri2 --disable-dri3 --disable-glx \
      --disable-int10-module --disable-vgahw --disable-vbe --disable-xdmcp \
      --disable-xinerama --without-dtrace --disable-systemd-logind --disable-secure-rpc \
      --disable-config-udev --disable-config-hal --without-systemd-daemon --disable-unit-tests \
      CC=${TC}gcc AR=${TC}ar RANLIB=${TC}ranlib \
      CFLAGS="--sysroot=$SYSROOT -I$PREFIX/include -DMAXHOSTNAMELEN=256 -DXOS_USE_MTSAFE_PWDAPI -D_POSIX_THREAD_SAFE_FUNCTIONS=200809L -DO_NOFOLLOW=0 -DSI_USER=0 -DHAVE_CBRT=1" \
      LDFLAGS="--sysroot=$SYSROOT -L$PREFIX/lib -L$SYSROOT/lib" \
      >/tmp/$NV-conf.log 2>&1 ) || { tail -30 /tmp/$NV-conf.log; fail "configure failed (see /tmp/$NV-conf.log)"; }
  # Record the glamor state so a later run detects a state change (above).
  if [ "$GLAMOR" = 1 ]; then touch "$GLAMOR_MARK"; else rm -f "$GLAMOR_MARK"; fi
fi

# --- 3. build the core archives ---
# --disable-xephyr means no server binary is linked here (Xphoenix is linked by
# build-xfbdev.sh from these archives), so `make` only compiles the libs. It can
# still exit non-zero on a trailing no-op target; the archive presence check below
# is the authoritative success gate.
# The glamor core patches (R/B-swap fix, DestroyPixmap chain) are applied by
# apply_core_patches above, which dry-runs each one first. They used to be applied
# here with a bare `patch -N`, on the assumption that -N makes a re-apply a no-op.
# It does not, reliably: once reverting the retired Y-flip patches shifted the
# surrounding lines, -N stopped recognising the R/B hunks as already applied and
# stacked SEVEN copies of glamor_phoenix_transfer_format into glamor_transfer.c,
# so libglamor.a failed to build ("redefinition of ..."). Never apply a patch
# without a dry-run guard.

echo "=== building $NV core (make -j$(nproc)) ==="
( cd "$KD" && make -j"$(nproc)" >/tmp/$NV-build.log 2>&1 ) \
  || echo "=== make returned non-zero — verifying archives directly ==="
all_present || { tail -40 /tmp/$NV-build.log; fail "core archives still missing after make (see /tmp/$NV-build.log)"; }

echo "=== OK: xorg-server $VER core archives built for aarch64-phoenix ==="
