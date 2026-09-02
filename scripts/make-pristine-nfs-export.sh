#!/usr/bin/env bash
#
# make-pristine-nfs-export.sh — build a CLEAN NFS export from the fresh clean rebuild
# (owner request, session ~212): fresh _fs/root + ONLY the handful of things the standard
# build still does not produce (e4 video tools), dropping ALL accumulated junk (GPU
# experiments, test litter, ML data, qdet dumps, nfsbench, broken symlinks). Safe SWAP:
# the old export is kept as a backup until verified, nothing is deleted in place.
#
# 2026-09-03: the GAMES now come entirely from the build. All five engines are framework
# ports installing into the rootfs (/usr/bin/{quakespasm,yquake2,quake3e,vkquake,
# supertuxkart}); their launchers are built into the same tree by
# scripts/build-rootfs-helpers.sh; their DATA is staged into the rootfs overlay by
# scripts/stage-game-data.sh (id1, baseq2, demoq3, SuperTuxKart data/ + stk-assets/) and
# therefore arrives through the step-1 rsync like everything else.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
FS="${RPI4B_BUILDROOT:-.buildroot}/_fs/${RPI4B_TARGET:-aarch64a72-generic-rpi4b}/root"

# The export to replace is DERIVED, never hardcoded. Prevents the failure this
# script hit on 2026-09-03: EXP was hardcoded to /srv/phoenix-rpi4-nfs while the
# export actually served as the Pi's "/" is the fsid=0 one in /etc/exports
# (/srv/phoenix-rpi4-nfs-gcc16). The pristine tree was swapped into a directory
# nothing mounts, so the Pi kept booting the old, stale export and the whole
# "clean export" step silently proved nothing. Same detection as
# sync-netboot-tree.sh:32 so the two can never disagree.
fsid0_export="$(awk '$0 ~ /fsid=0/ && $1 ~ /^\// { print $1; exit }' /etc/exports 2>/dev/null || true)"
EXP="${RPI4B_NFS_EXPORT:-${fsid0_export:-}}"
[ -n "$EXP" ] || {
	echo "FATAL: cannot determine the NFS export to replace."
	echo "       No fsid=0 entry in /etc/exports and RPI4B_NFS_EXPORT is unset."
	echo "       Refusing to guess — a wrong guess swaps a pristine tree into a"
	echo "       directory nothing mounts and hides a stale export."
	exit 1
}
[ -d "$EXP" ] || { echo "FATAL: resolved export $EXP does not exist"; exit 1; }
NEW="${EXP%/}.fresh"
BAK="${EXP%/}.PREV-cruft"
echo "== target export (fsid=0 / RPI4B_NFS_EXPORT): $EXP =="

[ -d "$FS/usr/bin" ] || { echo "FATAL: fresh rootfs $FS not found/complete"; exit 1; }

echo "== 1. fresh rootfs -> $NEW (clean base: core + X + ports + all five games + data) =="
sudo rm -rf "$NEW"; sudo mkdir -p "$NEW"
sudo rsync -a --exclude=/dev --exclude=/proc --exclude=/tmp --exclude=/mnt "$FS/" "$NEW/"
sudo mkdir -p "$NEW/dev" "$NEW/proc" "$NEW/tmp" "$NEW/mnt"

echo "== 2. overlay the little that still has no in-build path =="
# NO GAME DATA COPY-FORWARD either (2026-09-03). usr/share/{quake,quake2,quake3,
# supertuxkart} used to be carried over from the previous export "because it is not
# build-produced"; it is build-produced now — scripts/stage-game-data.sh puts all of it
# in the rootfs overlay, so it comes in with the step-1 rsync. Copying it forward would
# hide a broken staging step exactly the way the binary copy-forward below hid stale
# engines.
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
for p in bin/psh bin/busybox usr/bin/Xphoenix bin/xterm bin/wmaker \
	usr/bin/quakespasm usr/bin/vkquake usr/bin/yquake2 usr/bin/quake3e usr/bin/supertuxkart \
	bin/ram-stage-play usr/bin/quake2 usr/bin/quake3 bin/stk \
	usr/share/quake/id1/pak0.pak usr/share/quake2/baseq2/pak0.pak \
	usr/share/quake3/demoq3/pak0.pk3 usr/share/supertuxkart/data/stk_config.xml \
	usr/share/supertuxkart/stk-assets/karts; do
	[ -e "$NEW/$p" ] && echo "    OK  $p" || echo "    MISS $p"
done
echo "  -- sizes --"; du -sh "$NEW" 2>/dev/null

echo "== 4. SWAP: old -> $BAK (backup, kept), new -> $EXP =="
sudo rm -rf "$BAK"
sudo mv "$EXP" "$BAK"
sudo mv "$NEW" "$EXP"
echo "DONE: pristine export at $EXP ; old cruft backed up at $BAK (delete after owner confirms)"
