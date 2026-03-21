#!/usr/bin/env bash

set -euo pipefail

vm="${PHOENIX_VM:-phoenix-dev}"
buildroot="${PHOENIX_BUILDROOT:-/home/witoldbolt.guest/phoenix-buildroots/phoenix-rtos-project-copy}"
firmware_dir="${RPI4B_FIRMWARE_DIR:-/home/witoldbolt.guest/external/raspberrypi-firmware/boot}"
out_dir="${RPI4B_BOOTFS_OUT:-$buildroot/_boot/aarch64a72-generic-rpi4b/rpi4b-bootfs}"

limactl shell -y "$vm" -- /bin/bash -lc "
set -euo pipefail

buildroot='$buildroot'
firmware_dir='$firmware_dir'
out_dir='$out_dir'
staged_dir=\"\$buildroot/_boot/aarch64a72-generic-rpi4b/rpi4b\"

for req in start4.elf fixup4.dat; do
	if [ ! -f \"\$firmware_dir/\$req\" ]; then
		printf 'missing firmware file: %s\n' \"\$firmware_dir/\$req\" >&2
		exit 1
	fi
done

for req in config.txt kernel8.img loader.disk; do
	if [ ! -f \"\$staged_dir/\$req\" ]; then
		printf 'missing staged file: %s\n' \"\$staged_dir/\$req\" >&2
		exit 1
	fi
done

mkdir -p \"\$out_dir\"

cp -f \"\$firmware_dir/start4.elf\" \"\$out_dir/\"
cp -f \"\$firmware_dir/fixup4.dat\" \"\$out_dir/\"
cp -f \"\$staged_dir/config.txt\" \"\$out_dir/\"
cp -f \"\$staged_dir/kernel8.img\" \"\$out_dir/\"
cp -f \"\$staged_dir/loader.disk\" \"\$out_dir/\"

if [ -f \"\$staged_dir/bcm2711-rpi-4-b.dtb\" ]; then
	cp -f \"\$staged_dir/bcm2711-rpi-4-b.dtb\" \"\$out_dir/\"
elif [ -f \"\$firmware_dir/bcm2711-rpi-4-b.dtb\" ]; then
	cp -f \"\$firmware_dir/bcm2711-rpi-4-b.dtb\" \"\$out_dir/\"
fi

for opt in start4db.elf fixup4db.dat start4cd.elf fixup4cd.dat; do
	if [ -f \"\$firmware_dir/\$opt\" ]; then
		cp -f \"\$firmware_dir/\$opt\" \"\$out_dir/\"
	fi
done

find \"\$out_dir\" -maxdepth 1 -type f | sort
"
