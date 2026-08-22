#!/usr/bin/env bash
#
# Phoenix-RTOS — build the kdrive fbdev DDX server (Xphoenix) for aarch64-phoenix.
#
# Compiles tools/x11-port/src/xorg-server-1.20.14/hw/kdrive/fbdev/fbdev.c (the
# Phoenix /dev/fb0 kdrive backend) and links it against the already-built
# xorg-server 1.20.14 core archives + the X11 lib stack in /tmp/x11-phoenix,
# producing a static aarch64-phoenix `Xphoenix` server ELF.
#
# Host-side only. Does NOT touch the flagship image. Idempotent.
#
# Usage: build-xfbdev.sh [--stub | --glamor]
#   --stub     link the empty-hook fbdev_stub.c (link-closure de-risk) instead.
#   --glamor   E5/M1a: link glamor (2D GL accel) + our static Mesa GL into the
#              server. Compiles the DDX with -DGLAMOR_PHOENIX (enables the guarded
#              glamor_init call), adds glamor_phoenix_ctx.o + epoxy_shim.o +
#              libglamor.a + libGL-phoenix.a + libv3d-phoenix.a to the link, and
#              writes the full link stderr to /tmp/Xphoenix-glamor-link.log. Output
#              is Xphoenix-glamor (NOT published/staged over the shipping Xphoenix).
#
# Copyright 2026 Phoenix Systems
# Author: Witold Bołt
set -u

# Repo root derived from this script's own location (portable across checkouts).
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/../.." && pwd)"

TC=${ROOT}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-
SYSROOT=${ROOT}/.buildroot/_build/aarch64a72-generic-rpi4b/sysroot
PREFIX=/tmp/x11-phoenix
KD=${ROOT}/tools/x11-port/src/xorg-server-1.20.14
DDX=$KD/hw/kdrive/fbdev
CC=${TC}gcc

# glamor (E5/M1a) inputs — see the --glamor branch below.
SHIM=${ROOT}/tools/x11-port/glamor-shim
GPU_LIBS=${ROOT}/tools/.gpu-libs
MESA=${ROOT}/external/mesa
MESABUILD=/tmp/mesa-v3d-build
MESA_COMPAT=${ROOT}/tools/v3d-driver-port/phoenix_mesa_compat.h

SRCFILE=fbdev.c
OUT=Xphoenix
GLAMOR=0
DAEMON=0
case "${1:-}" in
  --stub)          SRCFILE=fbdev_stub.c; OUT=Xphoenix-stub ;;
  --glamor)        GLAMOR=1;             OUT=Xphoenix-glamor ;;
  # M3a: glamor X as a CLIENT of the v3d-server daemon. Same link as --glamor but the
  # in-process winsys backend (v3d_phoenix_winsys.o + v3d_phoenix_power.o, folded into
  # libv3d-phoenix.a) is swapped for libv3d-client.a, which RPCs phoenix_v3d_ioctl to
  # /dev/v3d-srv. X's GPU work then routes through the daemon (2c-test recipe scaled up).
  --glamor-daemon) GLAMOR=1; DAEMON=1;   OUT=Xphoenix-glamor-daemon ;;
esac

# Devices-repo home of the daemon client library (built into the swap below).
DEVV3D=${ROOT}/sources/phoenix-rtos-devices/gpu/rpi4-v3d

# Ensure the xorg-server core archives this script links against actually exist.
# Fetching/configuring/building the core was historically a MANUAL step (see
# PROGRESS.md), so a fresh clone reached the record-patch step below with no tree
# and failed ("RECORD rebuild FAIL"). build-xserver-core.sh automates it and is
# idempotent (a no-op once the archives are present), so it is safe to call every
# run and keeps this script self-contained for the showcase orchestrator.
"$(dirname "${BASH_SOURCE[0]:-$0}")/build-xserver-core.sh" || { echo "xorg-server core build failed"; exit 1; }

# The xorg-server source tree under src/ is a regenerable 3rd-party download; the
# DURABLE source-of-truth for the DDX is the tracked copy in tools/x11-port/ddx/.
# Sync it into the in-tree DDX dir before compiling so a fresh tree gets the backend.
DDX_SRC=${ROOT}/tools/x11-port/ddx
XKBDIR=${ROOT}/tools/x11-port/xkb
mkdir -p "$DDX"
cp "$DDX_SRC/fbdev.c" "$DDX/fbdev.c"
cp "$DDX_SRC/hid_evdev_map.h" "$DDX/hid_evdev_map.h"
[ -f "$DDX_SRC/fbdev_stub.c" ] && cp "$DDX_SRC/fbdev_stub.c" "$DDX/fbdev_stub.c"

# Durable core-source patches: the src/ tree is a regenerable download, so any
# fix to a core (non-DDX) file lives in tools/x11-port/patches/ and is (re)applied
# here, then the affected archive is rebuilt. Otherwise a relink would pull the
# STALE prebuilt archive (same stale-archive hazard as the toolchain libphoenix).
# patch -N is idempotent; `make -C record` is a fast no-op when nothing changed.
PATCHDIR=${ROOT}/tools/x11-port/patches
RECORD_PATCH="$PATCHDIR/xorg-server-1.20.14-record-malloc0.patch"
if [ -f "$RECORD_PATCH" ]; then
  echo "=== applying + rebuilding RECORD (malloc(0)->NULL assert guard) ==="
  patch -d "$KD" -p1 -N <"$RECORD_PATCH" >/dev/null 2>&1 || true
  make -C "$KD/record" >/dev/null 2>&1 || { echo "RECORD rebuild FAIL"; exit 1; }
fi

# Phoenix XKB fix: the kdrive server's XKB init forks `xkbcomp` (absent on the Pi),
# so it aborts before the dispatch loop. The durable patched ddxLoad.c (tracked in
# tools/x11-port/ddx/, mirroring how fbdev.c is the DDX source-of-truth) stages a
# compiled-in keymap instead of forking. We compile it fresh and link it BEFORE
# libxkb.a so the linker takes our XkbDDX* symbols and skips the stock archive member.
# The embedded keymap (builtin_keymap.h) is produced by xkb/gen-builtin-keymap.sh.
PATCHED_DDXLOAD="$DDX_SRC/ddxLoad.c"
if [ ! -f "$XKBDIR/builtin_keymap.h" ]; then
  echo "MISSING $XKBDIR/builtin_keymap.h — run xkb/gen-builtin-keymap.sh first"; exit 1
fi

# Compile flags mirror hw/kdrive/src/Makefile (DEFS + DEFAULT_INCLUDES + AM_CPPFLAGS + CFLAGS).
# Mirrors hw/kdrive/ephyr/Makefile (KDRIVE_CFLAGS + KDRIVE_INCS) so the kdrive
# headers (picturestr.h/randrstr.h/shadow.h/...) resolve exactly as they did when
# the core archives were compiled.
CFLAGS="--sysroot=$SYSROOT -fno-strict-aliasing -D_DEFAULT_SOURCE -D_BSD_SOURCE \
-DHAS_FCHOWN -DHAS_STICKY_DIR_BIT -DMAXHOSTNAMELEN=256 -DXOS_USE_MTSAFE_PWDAPI \
-D_POSIX_THREAD_SAFE_FUNCTIONS=200809L -DO_NOFOLLOW=0 -DSI_USER=0 \
-I$PREFIX/include -I$PREFIX/include/pixman-1 -I$PREFIX/include/freetype2"
INCS="-DHAVE_DIX_CONFIG_H -DHAVE_CONFIG_H \
-I$KD/include \
-I$KD/Xext -I$KD/composite -I$KD/damageext -I$KD/xfixes -I$KD/Xi -I$KD/mi \
-I$KD/miext/sync -I$KD/miext/shadow -I$KD/miext/damage \
-I$KD/render -I$KD/randr -I$KD/fb -I$KD/dbe -I$KD/present \
-I$KD/hw/kdrive/src -I$KD/hw/kdrive/linux -I$DDX"

# In --glamor mode the DDX gets -DGLAMOR_PHOENIX (enables the guarded glamor_init
# call in fbdevFinishInitScreen) and the glamor public-header path so <glamor.h>
# resolves. No epoxy/Mesa path is needed for the DDX itself (glamor.h pulls only
# X server headers).
GLAMOR_DDX_FLAGS=""
if [ "$GLAMOR" = 1 ]; then
  GLAMOR_DDX_FLAGS="-DGLAMOR_PHOENIX -I$KD/glamor"
fi

echo "=== compiling $SRCFILE ==="
$CC $CFLAGS $INCS $GLAMOR_DDX_FLAGS -c "$DDX/$SRCFILE" -o "$DDX/${SRCFILE%.c}.o" || { echo "COMPILE FAIL"; exit 1; }

# --- glamor context shim + epoxy shim (compiled against Mesa internal headers,
# the exact MFLAGS set the proven gl_x11_window.c harness uses) --------------
if [ "$GLAMOR" = 1 ]; then
  [ -f "$GPU_LIBS/libGL-phoenix.a" ]  || { echo "missing $GPU_LIBS/libGL-phoenix.a"; exit 1; }
  [ -f "$GPU_LIBS/libv3d-phoenix.a" ] || { echo "missing $GPU_LIBS/libv3d-phoenix.a"; exit 1; }
  [ -f "$KD/glamor/.libs/libglamor.a" ] || { echo "missing libglamor.a — configure core with --enable-glamor first"; exit 1; }

  MFLAGS="-O2 -g -ffreestanding -fno-strict-aliasing -Wno-error -Wno-undef \
-DUTIL_ARCH_LITTLE_ENDIAN=1 -DUTIL_ARCH_BIG_ENDIAN=0 -DHAVE_STRUCT_TIMESPEC \
-include $MESA_COMPAT \
-I$MESA/src -I$MESA/include -I$MESA/src/mesa -I$MESA/src/mapi -I$MESA/src/compiler \
-I$MESA/src/gallium/include -I$MESA/src/gallium/auxiliary -I$MESA/src/util -I$MESABUILD/src"

  echo "=== compiling glamor_phoenix_ctx.c (Mesa GL bring-up shim) ==="
  $CC --sysroot=$SYSROOT $MFLAGS -I"$KD/glamor" \
    -c "$SHIM/glamor_phoenix_ctx.c" -o /tmp/glamor_phoenix_ctx.o \
    || { echo "COMPILE FAIL (glamor_phoenix_ctx.c)"; exit 1; }

  echo "=== compiling epoxy_shim.c (GL version/extension query helpers) ==="
  $CC --sysroot=$SYSROOT $MFLAGS \
    -c "$SHIM/epoxy_shim.c" -o /tmp/epoxy_shim.o \
    || { echo "COMPILE FAIL (epoxy_shim.c)"; exit 1; }
fi

# Patched ddxLoad.c (XKB compiled-in-keymap fix). -I$XKBDIR resolves builtin_keymap.h.
echo "=== compiling patched ddxLoad.c (XKB no-xkbcomp fix) ==="
$CC $CFLAGS $INCS -I"$KD/xkb" -I"$XKBDIR" -c "$PATCHED_DDXLOAD" -o "$DDX/ddxLoad.o" \
  || { echo "COMPILE FAIL (ddxLoad.c)"; exit 1; }

# Server core archive list (from hw/kdrive/ephyr/Makefile KDRIVE_LIBS), .la -> .libs/*.a,
# PLUS dix/libmain.a (Xephyr supplies its own main(); we use the stock stubmain).
core_la=(
  dix/.libs/libmain.a
  dix/.libs/libdix.a
  hw/kdrive/src/.libs/libkdrive.a
  fb/.libs/libfb.a
  mi/.libs/libmi.a
  xfixes/.libs/libxfixes.a
  Xext/.libs/libXext.a
  Xext/.libs/libXvidmode.a
  Xext/.libs/libhashtable.a
  dbe/.libs/libdbe.a
  record/.libs/librecord.a
  randr/.libs/librandr.a
  render/.libs/librender.a
  damageext/.libs/libdamageext.a
  present/.libs/libpresent.a
  miext/sync/.libs/libsync.a
  miext/damage/.libs/libdamage.a
  miext/shadow/.libs/libshadow.a
  Xi/.libs/libXi.a
  Xi/.libs/libXistubs.a
  xkb/.libs/libxkb.a
  xkb/.libs/libxkbstubs.a
  composite/.libs/libcomposite.a
  config/.libs/libconfig.a
  os/.libs/libos.a
)
GROUP=""
for a in "${core_la[@]}"; do GROUP="$GROUP $KD/$a"; done

echo "=== linking $OUT ==="
# --start-group/--end-group: dix/os/mi/fb have circular refs (single-pass link fails).
# ddxLoad.o BEFORE the group: its XkbDDX* symbols satisfy the references first, so
# the linker never pulls the stock ddxLoad.o member out of libxkb.a (archive members
# are only extracted to resolve still-undefined symbols).
# -L$SYSROOT/lib comes FIRST so the implicit -lc/libphoenix resolves from the
# freshly built sysroot archive, not the toolchain's bundled
# aarch64-phoenix/lib/libphoenix.a. gcc's --sysroot does NOT redirect the
# built-in target lib dir for -lc (see `gcc -print-file-name=libc.a`), so
# without this -L a relink after a `--scope core` libphoenix rebuild silently
# links a STALE libphoenix (stale-core hazard) — e.g. it would drop the
# _signal_handler NULL-handler guard. An explicit -L precedes the built-in dir
# in ld's search order, so the sysroot copy wins.
if [ "$GLAMOR" = 1 ]; then
  # E5/M1a link. Everything that can cross-reference — the xserver core, glamor,
  # our GL context shims, and the Mesa GL + V3D archives — goes inside ONE
  # --start-group so any "undefined reference" ld reports is a genuine gap and
  # not a single-pass ordering artifact (the GL-entrypoint gap list is the whole
  # point of M1a). -lstdc++ + a 32 MB stack mirror the proven GL harness link.
  #
  # In --glamor-daemon (M3a) mode, swap the in-process winsys backend for the daemon
  # client: use libv3d-phoenix.a MINUS {v3d_phoenix_winsys.o, v3d_phoenix_power.o} plus
  # libv3d-client.a (phoenix_v3d_ioctl -> RPC to /dev/v3d-srv). Same recipe as
  # tools/v3d-driver-port/build-gl-smoke-daemon.py's 2c-test daemon variant.
  V3D_ARCHIVE="$GPU_LIBS/libv3d-phoenix.a"
  CLIENT_A=""
  if [ "$DAEMON" = 1 ]; then
    echo "=== M3a daemon swap: building libv3d-phoenix-daemon.a (minus winsys+power) + libv3d-client.a ==="
    V3D_ARCHIVE=/tmp/libv3d-phoenix-daemon.a
    cp "$GPU_LIBS/libv3d-phoenix.a" "$V3D_ARCHIVE"
    "${TC}gcc-ar" d "$V3D_ARCHIVE" v3d_phoenix_winsys.o v3d_phoenix_power.o \
      || { echo "ar d (winsys/power) FAIL"; exit 1; }
    $CC --sysroot=$SYSROOT -c "$DEVV3D/libv3d-client.c" -o /tmp/libv3d-client.o \
      -I"$DEVV3D" -I"$DEVV3D/uapi" -O2 -std=gnu11 \
      || { echo "COMPILE FAIL (libv3d-client.c)"; exit 1; }
    CLIENT_A=/tmp/libv3d-client.a
    rm -f "$CLIENT_A"; "${TC}gcc-ar" rcs "$CLIENT_A" /tmp/libv3d-client.o
  fi
  LINKLOG=/tmp/${OUT}-link.log
  $CC --sysroot=$SYSROOT -o "$DDX/$OUT" \
    "$DDX/${SRCFILE%.c}.o" "$DDX/ddxLoad.o" \
    /tmp/glamor_phoenix_ctx.o /tmp/epoxy_shim.o \
    -L$SYSROOT/lib -L$PREFIX/lib \
    -Wl,--start-group \
      $GROUP \
      "$KD/glamor/.libs/libglamor.a" \
      "$GPU_LIBS/libGL-phoenix.a" "$V3D_ARCHIVE" $CLIENT_A \
      -lpixman-1 -lXfont2 -lfontenc -lfreetype -lz -lXau -lXdmcp -lxkbfile -lmd -lm -lstdc++ \
    -Wl,--end-group \
    -Wl,-z,stack-size=33554432 \
    2> "$LINKLOG"
  rc=$?
  echo "=== glamor link stderr -> $LINKLOG ($(wc -l < "$LINKLOG") lines) ==="
else
  LINKLOG=$DDX/${OUT}-link.log
  $CC --sysroot=$SYSROOT -o "$DDX/$OUT" "$DDX/${SRCFILE%.c}.o" "$DDX/ddxLoad.o" \
    -L$SYSROOT/lib \
    -Wl,--start-group $GROUP -Wl,--end-group \
    -L$PREFIX/lib -lpixman-1 -lXfont2 -lfontenc -lfreetype -lz -lXau -lXdmcp -lxkbfile -lmd -lm \
    2> "$LINKLOG"
  rc=$?
fi
if [ $rc -ne 0 ]; then
  echo "LINK FAIL (rc=$rc). First undefined/errors:"
  grep -iE "undefined reference|error" "$LINKLOG" | head -40
  echo "(full log: $LINKLOG)"
  exit 1
fi
echo "=== OK: $DDX/$OUT ==="
file "$DDX/$OUT"
ls -l "$DDX/$OUT"

# Publish the full (non-stub) server to the tracked artifact location.
if [ "$OUT" = "Xphoenix" ]; then
  ART=${ROOT}/artifacts/x11
  mkdir -p "$ART"
  cp "$DDX/$OUT" "$ART/$OUT"
  echo "=== published -> $ART/$OUT ==="
  ls -l "$ART/$OUT"

  # Stage into the showcase rootfs bin, like the other X app build scripts
  # (e.g. build-xterm.sh). The showcase `--phase stage` driver sets
  # SHOWCASE_STAGE_DIR to _fs/<target>/root; without this the X server built +
  # published fine but never landed on the image, so /bin/Xphoenix (what
  # pl_phoenix_xlaunch/startx exec) was absent and X11 could not start.
  NFS="${SHOWCASE_STAGE_DIR:-}"
  if [ -n "$NFS" ]; then
    mkdir -p "$NFS/bin"
    cp "$DDX/$OUT" "$NFS/bin/$OUT"
    chmod 755 "$NFS/bin/$OUT"
    echo "=== staged -> $NFS/bin/$OUT ==="
  fi
fi
