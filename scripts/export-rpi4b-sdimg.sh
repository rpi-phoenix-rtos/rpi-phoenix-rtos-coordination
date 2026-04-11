#!/usr/bin/env bash

set -euo pipefail

vm="${PHOENIX_VM:-phoenix-dev}"
remote_image="${RPI4B_REMOTE_SDIMG:-/home/witoldbolt.guest/phoenix-buildroots/phoenix-rtos-project-copy/_boot/aarch64a72-generic-rpi4b/rpi4b-sd.img}"
out_dir="${RPI4B_EXPORT_DIR:-/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b}"
out_file="${RPI4B_EXPORT_SDIMG_PATH:-$out_dir/rpi4b-sd.img}"
tmp_file="${out_file}.tmp"
meta_file="${RPI4B_EXPORT_SDIMG_META:-${out_file}.meta.txt}"
tmp_meta="${meta_file}.tmp"
verify_script="${RPI4B_VERIFY_SCRIPT:-/Users/witoldbolt/phoenix-rpi/scripts/verify-rpi4b-sdimg.sh}"

cleanup()
{
	rm -f "$tmp_file"
	rm -f "$tmp_meta"
}

trap cleanup EXIT

mkdir -p "$out_dir"

limactl shell -y "$vm" -- test -f "$remote_image"
remote_size="$(limactl shell -y "$vm" -- stat -c%s "$remote_image" | tr -d '\r\n')"
remote_sha256="$(limactl shell -y "$vm" -- sha256sum "$remote_image" | awk '{print $1}')"
limactl shell -y "$vm" -- base64 -w0 "$remote_image" | base64 -d > "$tmp_file"
RPI4B_SDIMG_PATH="$tmp_file" \
	RPI4B_SDIMG_SHA256="$remote_sha256" \
	RPI4B_SDIMG_SIZE="$remote_size" \
	"$verify_script" >/dev/null

cat > "$tmp_meta" <<EOF
image_path=$out_file
size=$remote_size
sha256=$remote_sha256
vm=$vm
remote_image=$remote_image
EOF

mv "$tmp_file" "$out_file"
mv "$tmp_meta" "$meta_file"
trap - EXIT

printf 'Exported: %s\n' "$out_file"
printf 'Meta: %s\n' "$meta_file"
printf 'Size: %s\n' "$remote_size"
printf 'SHA256: %s\n' "$remote_sha256"
