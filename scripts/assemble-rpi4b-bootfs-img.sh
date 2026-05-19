#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
host_os="$(uname -s)"

vm="${PHOENIX_VM:-phoenix-dev}"
if [ "$host_os" = "Darwin" ]; then
	buildroot="${PHOENIX_BUILDROOT:-/home/witoldbolt.guest/phoenix-buildroots/phoenix-rtos-project-copy}"
else
	buildroot="${PHOENIX_BUILDROOT:-$repo_root/.buildroot}"
fi
bootfs_dir="${RPI4B_BOOTFS_DIR:-$buildroot/_boot/aarch64a72-generic-rpi4b/rpi4b-bootfs}"
image_path="${RPI4B_BOOTFS_IMG:-$buildroot/_boot/aarch64a72-generic-rpi4b/rpi4b-bootfs.img}"
image_size_mb="${RPI4B_BOOTFS_IMG_SIZE_MB:-64}"

run_shell() {
	if [ "$host_os" = "Darwin" ]; then
		limactl shell -y "$vm" -- /bin/bash -lc "$1"
	else
		/bin/bash -lc "$1"
	fi
}

run_shell "
set -euo pipefail

bootfs_dir='$bootfs_dir'
image_path='$image_path'
image_size_mb='$image_size_mb'

if [ ! -d \"\$bootfs_dir\" ]; then
	printf 'missing bootfs directory: %s\n' \"\$bootfs_dir\" >&2
	exit 1
fi

rm -f \"\$image_path\"
truncate -s \"\${image_size_mb}M\" \"\$image_path\"
mformat -i \"\$image_path\" -F -v PHOENIXPI ::
mcopy -i \"\$image_path\" -s \"\$bootfs_dir\"/* ::
printf 'Image: %s\n' \"\$image_path\"
mdir -i \"\$image_path\" ::
"
