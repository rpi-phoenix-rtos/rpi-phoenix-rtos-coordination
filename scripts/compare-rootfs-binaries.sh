#!/usr/bin/env bash
#
# compare-rootfs-binaries.sh — prove the SD image and the NFS export carry the
# SAME binaries as the build tree that produced them.
#
# The owner's requirement (2026-09-03): "comparison of the key binaries - new
# nfs rootfs vs new SD card image", so that a game verified over NFS is known
# to be the same artifact that ships on the card, ruling out build differences.
#
# Both roots are cut from .buildroot/_fs/<target>/root, so every shared file
# must be byte-identical. Any difference is either a staging bug or a stale
# artifact — the two failure modes that made months-old binaries survive on the
# export in the first place.
#
#   ./scripts/compare-rootfs-binaries.sh            # key binaries (fast)
#   ./scripts/compare-rootfs-binaries.sh --full     # + whole-tree diff
#
# Exit 0 only when every compared path matches in all three places.
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TARGET="${RPI4B_TARGET:-aarch64a72-generic-rpi4b}"
E2="${ROOT}/.buildroot/_boot/${TARGET}/part_rootfs.ext2"
FS="${ROOT}/.buildroot/_fs/${TARGET}/root"
TMP="$(mktemp -d)"
trap 'rm -rf "${TMP}"' EXIT

# The served export is whichever path carries fsid=0, not a hardcoded name —
# getting this wrong is how a "pristine" tree ended up somewhere nothing mounts
# (see make-pristine-nfs-export.sh).
EXP="${RPI4B_NFS_EXPORT:-$(awk '$0 ~ /fsid=0/ && $1 ~ /^\// { print $1; exit }' /etc/exports 2>/dev/null || true)}"

full=0
[ "${1:-}" = "--full" ] && full=1

# Everything a user would actually run, plus the five engines. Add to this list
# rather than trusting a spot check.
PATHS=(
	usr/bin/quakespasm
	usr/bin/yquake2
	usr/bin/quake3e
	usr/bin/vkquake
	usr/bin/supertuxkart
	bin/psh
	bin/busybox
	usr/bin/Xphoenix
	bin/python3
	bin/bash
)

die() {
	printf 'compare-rootfs-binaries: %s\n' "$1" >&2
	exit 2
}

[ -f "${E2}" ] || die "no ext2 rootfs image at ${E2} — run the sd-variant build first"
[ -d "${FS}" ] || die "no build rootfs tree at ${FS} — the fs/ports stages did not run"
[ -n "${EXP}" ] && [ -d "${EXP}" ] || die "cannot locate the fsid=0 NFS export (set RPI4B_NFS_EXPORT)"
command -v debugfs >/dev/null 2>&1 || die "debugfs not found (e2fsprogs) — needed to read the image without root"

printf '== comparing %d paths ==\n  build tree: %s\n  NFS export: %s\n  SD image:   %s\n\n' \
	"${#PATHS[@]}" "${FS}" "${EXP}" "${E2}"
printf '%-28s %-10s %-10s %-10s %s\n' "PATH" "BUILD" "EXPORT" "SDCARD" "VERDICT"

rc=0
for p in "${PATHS[@]}"; do
	bh="-" ; eh="-" ; sh="-"
	[ -f "${FS}/${p}" ] && bh="$(sha256sum "${FS}/${p}" | cut -c1-8)"
	[ -f "${EXP}/${p}" ] && eh="$(sha256sum "${EXP}/${p}" | cut -c1-8)"

	# debugfs writes its own chatter to stderr even on success; judge by the
	# dumped file, not by its exit status.
	rm -f "${TMP}/x"
	debugfs -R "dump /${p} ${TMP}/x" "${E2}" >/dev/null 2>&1
	[ -s "${TMP}/x" ] && sh="$(sha256sum "${TMP}/x" | cut -c1-8)"

	verdict="OK"
	if [ "${bh}" = "-" ]; then
		verdict="MISSING in build tree"
		rc=1
	elif [ "${eh}" = "-" ] || [ "${sh}" = "-" ]; then
		verdict="MISSING (export=${eh} sd=${sh})"
		rc=1
	elif [ "${bh}" != "${eh}" ] || [ "${bh}" != "${sh}" ]; then
		verdict="MISMATCH"
		rc=1
	fi
	printf '%-28s %-10s %-10s %-10s %s\n' "${p}" "${bh}" "${eh}" "${sh}" "${verdict}"
done

if [ "${full}" -eq 1 ]; then
	printf '\n== whole-tree diff (build tree vs NFS export) ==\n'
	# var/ and the runtime dirs are recreated or staged separately; /etc/fonts is
	# a known real gap (staged into the export only), so it is reported, not hidden.
	diff -r --brief --no-dereference \
		--exclude=dev --exclude=proc --exclude=tmp --exclude=mnt --exclude=var \
		"${FS}" "${EXP}" | head -50
	printf '(first 50 lines; empty above means the trees agree)\n'
fi

printf '\n'
if [ "${rc}" -eq 0 ]; then
	printf 'RESULT: every compared binary is byte-identical across build tree, NFS export and SD image\n'
else
	printf 'RESULT: differences found — a game verified over NFS is NOT the artifact on the card\n'
fi
exit "${rc}"
