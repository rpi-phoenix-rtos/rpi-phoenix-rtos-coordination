#!/usr/bin/env bash
#
# make-pristine-nfs-export.sh — build a CLEAN NFS export from the fresh clean rebuild
# (owner request, session ~212): fresh _fs/root + ONLY the legit hand-staged game/media
# content (Q2/Q3 engines+data, launchers, e4 video tools — not produced by the standard
# build), dropping ALL accumulated junk (GPU experiments, test litter, ML data, qdet
# dumps, nfsbench, broken symlinks). Safe SWAP: the old export is kept as a backup until
# verified, nothing is deleted in place.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
FS=".buildroot/aarch64a72-generic-rpi4b-fs-placeholder"   # overridden below
FS=".buildroot/_fs/aarch64a72-generic-rpi4b/root"
EXP="/srv/phoenix-rpi4-nfs"
NEW="/srv/phoenix-rpi4-nfs.fresh"
BAK="/srv/phoenix-rpi4-nfs.PREV-cruft"

[ -d "$FS/usr/bin" ] || { echo "FATAL: fresh rootfs $FS not found/complete"; exit 1; }

echo "== 1. fresh rootfs -> $NEW (clean base: core + Q1 games + X + ports + id1 data) =="
sudo rm -rf "$NEW"; sudo mkdir -p "$NEW"
sudo rsync -a --exclude=/dev --exclude=/proc --exclude=/tmp --exclude=/mnt "$FS/" "$NEW/"
sudo mkdir -p "$NEW/dev" "$NEW/proc" "$NEW/tmp" "$NEW/mnt"

echo "== 2. overlay legit hand-staged game/media content from the old export =="
# Q2/Q3 game DATA (not build-produced; Q1 id1 is already in the fresh build)
for d in usr/share/quake2 usr/share/quake3; do
	[ -d "$EXP/$d" ] && { sudo mkdir -p "$NEW/$(dirname "$d")"; sudo cp -a "$EXP/$d" "$NEW/$(dirname "$d")/"; echo "  + $d"; }
done
# NO BINARY COPY-FORWARD. Owner directive 2026-09-03: "Why are you hand copying
# old binaries. This make zero sense! Never do this!"
#
# This block used to carry usr/bin/{yquake2,quake3e,quake2,quake3} and the
# bin/{quakespasm,vkquake,ram-stage-play,...} launchers over from the previous
# export "because they are not in the standard build". That defeats the purpose
# of a pristine export: it hides whichever ports the build does not actually
# produce, and it means the Pi runs binaries nobody can rebuild -- the two
# engines carried this way were still dated Aug 28, i.e. from before we
# regenerated their port patches, so tests against them proved nothing about
# current source. Anything executable must now come from the build; if it is
# missing after this script, the BUILD is what needs fixing.
# Same rule for CPython's stdlib: the python port installs it, so a build that
# does not produce it is a build to fix, not a tree to patch up by hand.

echo "== 3. verify $NEW is clean (no junk) + complete =="
echo "  -- junk check (should list NOTHING) --"
ls "$NEW" 2>/dev/null | grep -iE "stackbomb|csd-matmul|gl-smoke|gpu-.*\.sh|^rpi4-v3d$|nfsbench|qdet-|stories.*\.bin|tok.*\.bin|dump\.rdb|waltest|selftest|selfcheck|\.jq$|\.sql$|\.test$|dectest|ctypestest|ext_test|sotest|console_history|index\.html|^&1$|^\(null\)|^aaaa|symShort|test_stat|test_readdir|linuxrc|redis-|cu-.*\.txt|\.raw$" || echo "    (clean — no junk at root)"
echo "  -- completeness check --"
for p in bin/psh bin/busybox usr/bin/Xphoenix bin/xterm bin/wmaker usr/bin/rpi4-quake usr/bin/rpi4-vkquake usr/share/quake/id1/pak0.pak usr/share/quake2/baseq2/pak0.pak usr/share/quake3/demoq3/pak0.pk3 usr/bin/yquake2 usr/bin/quake3e; do
	[ -e "$NEW/$p" ] && echo "    OK  $p" || echo "    MISS $p"
done
echo "  -- sizes --"; du -sh "$NEW" 2>/dev/null

echo "== 4. SWAP: old -> $BAK (backup, kept), new -> $EXP =="
sudo rm -rf "$BAK"
sudo mv "$EXP" "$BAK"
sudo mv "$NEW" "$EXP"
echo "DONE: pristine export at $EXP ; old cruft backed up at $BAK (delete after owner confirms)"
