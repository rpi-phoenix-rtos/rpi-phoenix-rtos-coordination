#!/bin/bash
#
# Flash the built SD image to a physical card. macOS-oriented (uses
# diskutil); the target device MUST be supplied via RPI4B_SD_DEV — there
# is NO default, because a wrong device can destroy the host's disks.
#
#   RPI4B_SD_DEV   macOS disk identifier of the SD card, e.g. "disk4"
#                  (find it with `diskutil list`). Pass just the disk
#                  name; this script derives /dev/<disk> (buffered, for
#                  diskutil) and /dev/r<disk> (raw, for the fast dd).
#
# Example:
#   RPI4B_SD_DEV=disk4 ./scripts/write-sdimg.sh

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." && pwd)"

sd_dev="${RPI4B_SD_DEV:-}"
if [ -z "$sd_dev" ]; then
	echo "write-sdimg: RPI4B_SD_DEV is not set." >&2
	echo "write-sdimg: set it to your SD card's disk identifier (e.g. RPI4B_SD_DEV=disk4)." >&2
	echo "write-sdimg: run 'diskutil list' first to identify the correct device;" >&2
	echo "write-sdimg: choosing the wrong device can ERASE important data." >&2
	exit 1
fi

# Strip any leading /dev/ or r the user may have included.
sd_dev="${sd_dev#/dev/}"
sd_dev="${sd_dev#r}"

buffered_node="/dev/${sd_dev}"
raw_node="/dev/r${sd_dev}"
image_path="${RPI4B_SDIMG_PATH:-$repo_root/artifacts/rpi4b/rpi4b-sd.img}"

echo "WARNING: this only works on macOS and will write to ${buffered_node}"
echo "WARNING: if ${buffered_node} is not your SD card you might corrupt your system (i.e. erase important data)"

"$repo_root/scripts/verify-rpi4b-sdimg.sh"

diskutil unmountDisk "$buffered_node"

sudo dd if="$image_path" of="$raw_node" bs=4m

sync

diskutil eject "$buffered_node"
