#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
host_os="$(uname -s)"

vm="${PHOENIX_VM:-phoenix-dev}"
if [ "$host_os" = "Darwin" ]; then
	buildroot="${PHOENIX_BUILDROOT:-/home/witoldbolt.guest/phoenix-buildroots/phoenix-rtos-project-copy}"
	firmware_dir="${RPI4B_FIRMWARE_DIR:-/home/witoldbolt.guest/external/raspberrypi-firmware/boot}"
else
	buildroot="${PHOENIX_BUILDROOT:-$repo_root/.buildroot}"
	firmware_dir="${RPI4B_FIRMWARE_DIR:-$repo_root/.bootblobs}"
fi
out_dir="${RPI4B_BOOTFS_OUT:-$buildroot/_boot/aarch64a72-generic-rpi4b/rpi4b-bootfs}"

run_shell() {
	if [ "$host_os" = "Darwin" ]; then
		limactl shell -y "$vm" -- /bin/bash -lc "$1"
	else
		/bin/bash -lc "$1"
	fi
}

run_shell "
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

for req in config.txt kernel8.img loader.disk phoenix-armstub8-rpi4.bin; do
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
cp -f \"\$staged_dir/phoenix-armstub8-rpi4.bin\" \"\$out_dir/\"

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

rm -rf \"\$out_dir/overlays\"
mkdir -p \"\$out_dir/overlays\"

if grep -Eq '^dtoverlay=miniuart-bt$' \"\$staged_dir/config.txt\"; then
	if [ ! -f \"\$firmware_dir/overlays/miniuart-bt.dtbo\" ]; then
		printf 'missing firmware overlay: %s\n' \"\$firmware_dir/overlays/miniuart-bt.dtbo\" >&2
		exit 1
	fi

	cp -f \"\$firmware_dir/overlays/miniuart-bt.dtbo\" \"\$out_dir/overlays/\"
fi

if [ -d \"\$staged_dir/overlays\" ]; then
	cp -f \"\$staged_dir/overlays\"/*.dtbo \"\$out_dir/overlays/\" 2>/dev/null || true
fi

find \"\$out_dir\" -maxdepth 2 -type f | sort
"
