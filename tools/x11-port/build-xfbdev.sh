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
# Usage: build-xfbdev.sh [--stub]
#   --stub   link the empty-hook fbdev_stub.c (link-closure de-risk) instead.
#
# Copyright 2026 Phoenix Systems
# Author: Witold Bołt
set -u

TC=/home/houp/phoenix-rpi/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-
SYSROOT=/home/houp/phoenix-rpi/.buildroot/_build/aarch64a72-generic-rpi4b/sysroot
PREFIX=/tmp/x11-phoenix
KD=/home/houp/phoenix-rpi/tools/x11-port/src/xorg-server-1.20.14
DDX=$KD/hw/kdrive/fbdev
CC=${TC}gcc

SRCFILE=fbdev.c
OUT=Xphoenix
if [ "${1:-}" = "--stub" ]; then SRCFILE=fbdev_stub.c; OUT=Xphoenix-stub; fi

# The xorg-server source tree under src/ is a regenerable 3rd-party download; the
# DURABLE source-of-truth for the DDX is the tracked copy in tools/x11-port/ddx/.
# Sync it into the in-tree DDX dir before compiling so a fresh tree gets the backend.
DDX_SRC=/home/houp/phoenix-rpi/tools/x11-port/ddx
XKBDIR=/home/houp/phoenix-rpi/tools/x11-port/xkb
mkdir -p "$DDX"
cp "$DDX_SRC/fbdev.c" "$DDX/fbdev.c"
[ -f "$DDX_SRC/fbdev_stub.c" ] && cp "$DDX_SRC/fbdev_stub.c" "$DDX/fbdev_stub.c"

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

echo "=== compiling $SRCFILE ==="
$CC $CFLAGS $INCS -c "$DDX/$SRCFILE" -o "$DDX/${SRCFILE%.c}.o" || { echo "COMPILE FAIL"; exit 1; }

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
$CC --sysroot=$SYSROOT -o "$DDX/$OUT" "$DDX/${SRCFILE%.c}.o" "$DDX/ddxLoad.o" \
  -Wl,--start-group $GROUP -Wl,--end-group \
  -L$PREFIX/lib -lpixman-1 -lXfont2 -lfontenc -lfreetype -lz -lXau -lXdmcp -lxkbfile -lmd -lm \
  2> "$DDX/${OUT}-link.log"
rc=$?
if [ $rc -ne 0 ]; then
  echo "LINK FAIL (rc=$rc). First undefined/errors:"
  grep -iE "undefined reference|error" "$DDX/${OUT}-link.log" | head -40
  echo "(full log: $DDX/${OUT}-link.log)"
  exit 1
fi
echo "=== OK: $DDX/$OUT ==="
file "$DDX/$OUT"
ls -l "$DDX/$OUT"

# Publish the full (non-stub) server to the tracked artifact location.
if [ "$OUT" = "Xphoenix" ]; then
  ART=/home/houp/phoenix-rpi/artifacts/x11
  mkdir -p "$ART"
  cp "$DDX/$OUT" "$ART/$OUT"
  echo "=== published -> $ART/$OUT ==="
  ls -l "$ART/$OUT"
fi
