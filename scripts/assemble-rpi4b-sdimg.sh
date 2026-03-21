#!/usr/bin/env bash

set -euo pipefail

vm="${PHOENIX_VM:-phoenix-dev}"
buildroot="${PHOENIX_BUILDROOT:-/home/witoldbolt.guest/phoenix-buildroots/phoenix-rtos-project-copy}"
bootfs_img="${RPI4B_BOOTFS_IMG:-$buildroot/_boot/aarch64a72-generic-rpi4b/rpi4b-bootfs.img}"
sdimg_path="${RPI4B_SDIMG_PATH:-$buildroot/_boot/aarch64a72-generic-rpi4b/rpi4b-sd.img}"
part_start_sectors="${RPI4B_SDIMG_PART_START_SECTORS:-2048}"
tail_sectors="${RPI4B_SDIMG_TAIL_SECTORS:-2048}"

limactl shell -y "$vm" -- /bin/bash -lc "
set -euo pipefail

bootfs_img='$bootfs_img'
sdimg_path='$sdimg_path'
part_start_sectors='$part_start_sectors'
tail_sectors='$tail_sectors'
sector_size=512

if [ ! -f \"\$bootfs_img\" ]; then
	printf 'missing FAT image: %s\n' \"\$bootfs_img\" >&2
	exit 1
fi

bootfs_bytes=\$(stat -c %s \"\$bootfs_img\")
part_sectors=\$(((bootfs_bytes + sector_size - 1) / sector_size))
total_sectors=\$((part_start_sectors + part_sectors + tail_sectors))
part_offset_bytes=\$((part_start_sectors * sector_size))

rm -f \"\$sdimg_path\"
truncate -s \$((total_sectors * sector_size)) \"\$sdimg_path\"

printf 'label: dos\nunit: sectors\n\nstart=%s, size=%s, type=c, bootable\n' \
	\"\$part_start_sectors\" \"\$part_sectors\" | sfdisk \"\$sdimg_path\" >/dev/null

dd if=\"\$bootfs_img\" of=\"\$sdimg_path\" bs=\"\$sector_size\" seek=\"\$part_start_sectors\" conv=notrunc status=none

printf 'Image: %s\n' \"\$sdimg_path\"
fdisk -l \"\$sdimg_path\"
printf 'FAT offset: %s\n' \"\$part_offset_bytes\"
mdir -i \"\$sdimg_path@@\$part_offset_bytes\" ::
"
