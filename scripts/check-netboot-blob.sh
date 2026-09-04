#!/usr/bin/env bash
#
# check-netboot-blob.sh — say which ROOTFS the TFTP boot blob is configured for,
# and refuse the mismatch.
#
# WHY THIS EXISTS
#
# The TFTP directory dnsmasq serves is the build tree itself
# (.buildroot/_boot/<target>/rpi4b-bootfs — see netboot-server.sh:25), so a
# `--variant sd` build OVERWRITES the netboot loader.disk in place. Boot that
# with no card in the Pi and the Pi does exactly what the owner saw on
# 2026-09-04: firmware TFTPs the loader fine, then sdstorage_srv looks for
# /dev/mmcblk0p2 that is not there, no root is ever mounted, and no user-space
# program runs. Nothing reports an error -- the boot just stops being useful,
# and the failure looks like whatever you were testing.
#
# The variant is written into the blob as plo's syspage root entry, so it can be
# read out of the artifact instead of remembered:
#
#   nfsroot : nfs;/;<server>;/;v4;takeover
#   sd      : bcm2711-emmc;-r;/dev/mmcblk0p2:ext2
#
# Usage:
#   ./scripts/check-netboot-blob.sh                 # require nfsroot (netboot)
#   ./scripts/check-netboot-blob.sh --expect sd     # require the SD variant
#   ./scripts/check-netboot-blob.sh --print         # just print the variant
#
# Exit 0 when the blob matches, 3 on a mismatch (naming the rebuild command),
# 2 when there is no blob to check.
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

set -uo pipefail

repo="${PHOENIX_RPI_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
target="${RPI4B_TARGET:-aarch64a72-generic-rpi4b}"
buildroot="${RPI4B_BUILDROOT:-${repo}/.buildroot}"
blob="${buildroot}/_boot/${target}/rpi4b-bootfs/loader.disk"

expect="nfsroot"
print_only=0
while [ "$#" -gt 0 ]; do
	case "$1" in
	--expect) expect="${2:?--expect needs nfsroot|sd}"; shift 2 ;;
	--print) print_only=1; shift ;;
	--blob) blob="${2:?--blob needs a path}"; shift 2 ;;
	-h|--help) awk 'NR>=2 && NR<=31' "${BASH_SOURCE[0]}"; exit 0 ;;
	*) printf 'check-netboot-blob: unknown argument %s\n' "$1" >&2; exit 2 ;;
	esac
done

if [ ! -f "${blob}" ]; then
	printf 'check-netboot-blob: no boot blob at %s\n' "${blob}" >&2
	exit 2
fi

# COUNT, never `grep -q`: under `set -o pipefail` a -q exits on the first match
# and closes the pipe, `strings` dies of SIGPIPE and the pipeline status becomes
# 141 -- so a -q check inverts its own meaning exactly when the string IS
# present. That single mistake made both variants of this guard fail on
# 2026-09-04 (one failed open on a real SD blob, one refused a good nfsroot
# blob). Same trap is documented in verify-sd-image-contents.sh.
blob_has() { [ "$(strings -a "${blob}" 2>/dev/null | grep -c -- "$1" || true)" -gt 0 ]; }

variant="unknown"
if blob_has 'nfs;/'; then
	variant="nfsroot"
elif blob_has 'mmcblk0p2'; then
	variant="sd"
fi

printf 'boot blob: %s\n' "${blob}"
printf 'built:     %s\n' "$(date -r "${blob}" '+%Y-%m-%d %H:%M:%S')"
printf 'rootfs:    %s\n' "${variant}"

[ "${print_only}" -eq 1 ] && exit 0
[ "${variant}" = "${expect}" ] && exit 0

printf '\nREFUSING: this blob boots a %s rootfs, but a %s one is required.\n' \
	"${variant}" "${expect}" >&2
if [ "${expect}" = "nfsroot" ]; then
	printf 'Rebuild the netboot blob (a --variant sd build overwrites it in place):\n' >&2
	printf '  RPI4B_ROOTFS_BLOCKS=1572864 ./scripts/rebuild-rpi4b-fast.sh \\\n' >&2
	printf '    --scope project --variant nfsroot --skip-prepare\n' >&2
	printf 'then push the rootfs the Pi will mount:\n' >&2
	printf '  ./scripts/sync-netboot-tree.sh\n' >&2
else
	printf 'Rebuild the SD image:  ./scripts/rebuild-rpi4b-fast.sh --variant sd\n' >&2
fi
exit 3
