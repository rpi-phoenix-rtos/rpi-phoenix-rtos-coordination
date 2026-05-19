#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
host_os="$(uname -s)"

vm="${PHOENIX_VM:-phoenix-dev}"
if [ "$host_os" = "Darwin" ]; then
	remote_image="${RPI4B_REMOTE_BOOTFS_IMG:-/home/witoldbolt.guest/phoenix-buildroots/phoenix-rtos-project-copy/_boot/aarch64a72-generic-rpi4b/rpi4b-bootfs.img}"
	out_dir="${RPI4B_EXPORT_DIR:-/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b}"
else
	remote_image="${RPI4B_REMOTE_BOOTFS_IMG:-$repo_root/.buildroot/_boot/aarch64a72-generic-rpi4b/rpi4b-bootfs.img}"
	out_dir="${RPI4B_EXPORT_DIR:-$repo_root/artifacts/rpi4b}"
fi
out_file="${RPI4B_EXPORT_PATH:-$out_dir/rpi4b-bootfs.img}"
tmp_file="${out_file}.tmp"

mkdir -p "$out_dir"

if [ "$host_os" = "Darwin" ]; then
	limactl shell -y "$vm" -- test -f "$remote_image"
	limactl shell -y "$vm" -- cat "$remote_image" > "$tmp_file"
else
	test -f "$remote_image" || { printf 'missing %s\n' "$remote_image" >&2; exit 1; }
	cp "$remote_image" "$tmp_file"
fi
mv "$tmp_file" "$out_file"

printf 'Exported: %s\n' "$out_file"
wc -c < "$out_file"
