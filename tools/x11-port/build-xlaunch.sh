#!/usr/bin/env bash
#
# Phoenix-RTOS — build pl_phoenix_xlaunch for aarch64-phoenix.
#
# pl_phoenix_xlaunch is an xinit-style launcher: psh runs it foreground, and it
# forks the Xphoenix server, waits for the X listening socket, then forks an X
# client (xeyes/twm) with DISPLAY=:0. This works around psh having no job
# control (`&` is a redirect), so a server + client can be started from one psh
# command. See tools/x11-port/launcher/pl_phoenix_xlaunch.c.
#
# Static aarch64-phoenix ELF; libc only (vfork/execve/stat/waitpid/mkdir/kill).
# Host-side only. Does NOT touch the flagship image. Idempotent.
#
# Copyright 2026 Phoenix Systems
# Author: Witold Bołt
set -u

TC=/home/houp/phoenix-rpi/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-
SYSROOT=/home/houp/phoenix-rpi/.buildroot/_build/aarch64a72-generic-rpi4b/sysroot
SRC=/home/houp/phoenix-rpi/tools/x11-port/launcher/pl_phoenix_xlaunch.c
OUT=pl_phoenix_xlaunch
CC=${TC}gcc

LAUNCHDIR=/home/houp/phoenix-rpi/tools/x11-port/launcher
ART=/home/houp/phoenix-rpi/artifacts/x11

echo "=== compiling + linking $OUT (static aarch64-phoenix) ==="
$CC --sysroot=$SYSROOT -static -Wall -Wextra -O2 \
  -o "$LAUNCHDIR/$OUT" "$SRC" \
  2> "$LAUNCHDIR/${OUT}-build.log"
rc=$?
if [ $rc -ne 0 ]; then
  echo "BUILD FAIL (rc=$rc):"
  cat "$LAUNCHDIR/${OUT}-build.log"
  exit 1
fi
# surface any warnings (none expected)
if [ -s "$LAUNCHDIR/${OUT}-build.log" ]; then
  echo "--- build warnings ---"
  cat "$LAUNCHDIR/${OUT}-build.log"
fi

echo "=== OK: $LAUNCHDIR/$OUT ==="
file "$LAUNCHDIR/$OUT"
ls -l "$LAUNCHDIR/$OUT"

# 0-undefined check (static ELF: there should be no UND symbols at all).
echo "=== undefined-symbol check ==="
und=$(${TC}nm -u "$LAUNCHDIR/$OUT" 2>/dev/null)
if [ -n "$und" ]; then
  echo "WARNING: undefined symbols present:"
  echo "$und"
else
  echo "0 undefined symbols (fully static)."
fi

# Publish to the tracked artifact location.
mkdir -p "$ART"
cp "$LAUNCHDIR/$OUT" "$ART/$OUT"
echo "=== published -> $ART/$OUT ==="
ls -l "$ART/$OUT"

# Stage to the NFS export so a netboot picks the new binary up. The launcher is
# installed under TWO names — pl_phoenix_xlaunch (explicit form) and startx
# (convenience/desktop mode keyed on argv[0]). startx is a plain COPY, not a
# symlink, so BOTH must be refreshed or `startx desktop` runs a stale binary.
NFS_BIN=/srv/phoenix-rpi4-nfs/bin
if [ -d "$NFS_BIN" ]; then
  cp "$LAUNCHDIR/$OUT" "$NFS_BIN/$OUT"
  cp "$LAUNCHDIR/$OUT" "$NFS_BIN/startx"
  chmod 755 "$NFS_BIN/$OUT" "$NFS_BIN/startx"
  echo "=== staged -> $NFS_BIN/{$OUT,startx} ==="
  ls -l "$NFS_BIN/$OUT" "$NFS_BIN/startx"
else
  echo "=== NFS export $NFS_BIN not present — skipped staging (artifact only) ==="
fi
