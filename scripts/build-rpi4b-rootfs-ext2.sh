#!/usr/bin/env bash
#
# Build a minimal ext2 rootfs image for the Raspberry Pi 4 (#120) and, if the
# FAT bootfs image is present, a 2-partition SD image (FAT boot + ext2 root).
#
# This is the "no-card" half of the SD-rootfs work: it produces the image
# artifacts on the host. Booting from the ext2 root (the user.plo.yaml `-r
# /dev/mmcblk0p2:ext2` flip + rc.psh conversion) is a separate, hardware-
# validated step — see docs/done/2026-06-03-sd-rootfs-plan.md.
#
# The rootfs tree is whatever the project build already staged under
# _fs/<target>/root (psh + applets + servers), plus the runtime mountpoints
# (/dev /tmp /root /mnt). No genext2fs needed — uses mke2fs -d.

set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
buildroot="${RPI4B_BUILDROOT:-$repo_root/.buildroot}"
target="aarch64a72-generic-rpi4b"

rootfs_tree="${RPI4B_ROOTFS_TREE:-$buildroot/_fs/$target/root}"
boot_dir="$buildroot/_boot/$target"
rootfs_img="${RPI4B_ROOTFS_IMG:-$boot_dir/part_rootfs.ext2}"
bootfs_img="${RPI4B_BOOTFS_IMG:-$boot_dir/rpi4b-bootfs.img}"
sd2_img="${RPI4B_SDIMG2_PATH:-$boot_dir/rpi4b-sd-2part.img}"

# ext2 image geometry
size_blocks="${RPI4B_ROOTFS_BLOCKS:-262144}" # 1 KiB blocks -> 256 MiB
sector_size=512
part_start_sectors="${RPI4B_SDIMG_PART_START_SECTORS:-2048}"
gap_sectors=2048   # 1 MiB-aligned gap between partitions
tail_sectors="${RPI4B_SDIMG_TAIL_SECTORS:-2048}"

if [ ! -d "$rootfs_tree" ]; then
	printf 'missing rootfs tree: %s\n  (run a project build first, e.g. ./scripts/rebuild-rpi4b-fast.sh --scope core)\n' "$rootfs_tree" >&2
	exit 1
fi

# Stage a copy with the runtime mountpoints the diskless tree lacks.
stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
cp -a "$rootfs_tree"/. "$stage"/
mkdir -p "$stage/dev" "$stage/tmp" "$stage/root" "$stage/mnt"

printf '=== rootfs tree (%s) ===\n' "$(du -sh "$stage" | cut -f1)"
ls "$stage"

# Deterministic, fsck-clean ext2 image populated from the directory.
mke2fs -q -t ext2 -b 1024 -i 2048 -d "$stage" \
	-U 00000000-0000-0000-0000-000000000000 -L rootfs \
	"$rootfs_img" "$size_blocks"
e2fsck -fn "$rootfs_img" >/dev/null && printf 'ext2 rootfs image: %s (fsck-clean)\n' "$rootfs_img"

# 2-partition SD image: FAT boot (type c, from rpi4b-bootfs.img) + ext2 root
# (type 83). Only if the FAT bootfs image exists.
if [ -f "$bootfs_img" ]; then
	bootfs_bytes=$(stat -c %s "$bootfs_img")
	rootfs_bytes=$(stat -c %s "$rootfs_img")
	fat_sectors=$(((bootfs_bytes + sector_size - 1) / sector_size))
	root_sectors=$(((rootfs_bytes + sector_size - 1) / sector_size))
	part2_start=$((part_start_sectors + fat_sectors + gap_sectors))
	total_sectors=$((part2_start + root_sectors + tail_sectors))

	rm -f "$sd2_img"
	truncate -s $((total_sectors * sector_size)) "$sd2_img"
	printf 'label: dos\nunit: sectors\n\nstart=%s, size=%s, type=c, bootable\nstart=%s, size=%s, type=83\n' \
		"$part_start_sectors" "$fat_sectors" "$part2_start" "$root_sectors" | sfdisk "$sd2_img" >/dev/null
	dd if="$bootfs_img" of="$sd2_img" bs="$sector_size" seek="$part_start_sectors" conv=notrunc status=none
	dd if="$rootfs_img" of="$sd2_img" bs="$sector_size" seek="$part2_start" conv=notrunc status=none
	printf '\n=== 2-partition SD image: %s ===\n' "$sd2_img"
	fdisk -l "$sd2_img"
else
	printf 'NOTE: %s absent; built rootfs image only (no 2-partition SD image).\n' "$bootfs_img"
fi
