#!/usr/bin/env bash

set -euo pipefail

vm="${PHOENIX_VM:-phoenix-dev}"
remote_image="${RPI4B_REMOTE_BOOTFS_IMG:-/home/witoldbolt.guest/phoenix-buildroots/phoenix-rtos-project-copy/_boot/aarch64a72-generic-rpi4b/rpi4b-bootfs.img}"
out_dir="${RPI4B_EXPORT_DIR:-/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b}"
out_file="${RPI4B_EXPORT_PATH:-$out_dir/rpi4b-bootfs.img}"
tmp_file="${out_file}.tmp"

mkdir -p "$out_dir"

limactl shell -y "$vm" -- test -f "$remote_image"
limactl shell -y "$vm" -- cat "$remote_image" > "$tmp_file"
mv "$tmp_file" "$out_file"

printf 'Exported: %s\n' "$out_file"
wc -c < "$out_file"
