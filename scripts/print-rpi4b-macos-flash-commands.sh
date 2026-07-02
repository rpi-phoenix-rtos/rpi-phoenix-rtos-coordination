#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." && pwd)"

if [ $# -ne 1 ]; then
	printf 'usage: %s diskN\n' "$(basename "$0")" >&2
	exit 1
fi

disk="$1"
image_path="${RPI4B_SDIMG_PATH:-$repo_root/artifacts/rpi4b/rpi4b-sd.img}"

case "$disk" in
	disk[0-9]*)
		;;
	*)
		printf 'expected disk identifier like disk4, got: %s\n' "$disk" >&2
		exit 1
		;;
esac

cat <<EOF
Verify the image first:
  $repo_root/scripts/verify-rpi4b-sdimg.sh

Inspect disks:
  diskutil list

Unmount the whole target disk:
  diskutil unmountDisk /dev/$disk

Write the image:
  sudo dd if=$image_path of=/dev/r$disk bs=4m

Flush and eject:
  sync
  diskutil eject /dev/$disk
EOF
