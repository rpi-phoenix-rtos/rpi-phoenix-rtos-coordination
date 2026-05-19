#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
image_path="${RPI4B_SDIMG_PATH:-$repo_root/artifacts/rpi4b/rpi4b-sd.img}"
meta_path="${RPI4B_SDIMG_META:-${image_path}.meta.txt}"
expected_sha256="${RPI4B_SDIMG_SHA256:-}"
expected_size="${RPI4B_SDIMG_SIZE:-}"

if [ ! -f "$image_path" ]; then
	printf 'missing image: %s\n' "$image_path" >&2
	exit 1
fi

if [ -z "$expected_sha256" ] || [ -z "$expected_size" ]; then
	if [ -f "$meta_path" ]; then
		while IFS='=' read -r key value; do
			case "$key" in
				sha256)
					[ -z "$expected_sha256" ] && expected_sha256="$value"
					;;
				size)
					[ -z "$expected_size" ] && expected_size="$value"
					;;
			esac
		done < "$meta_path"
	fi
fi

if [ -z "$expected_sha256" ] || [ -z "$expected_size" ]; then
	printf 'missing expected image metadata: set RPI4B_SDIMG_SHA256/RPI4B_SDIMG_SIZE or provide %s\n' "$meta_path" >&2
	exit 1
fi

actual_size="$(wc -c < "$image_path" | tr -d '[:space:]')"
actual_sha256="$(shasum -a 256 "$image_path" | awk '{print $1}')"
partition_offset="$(python3 - "$image_path" <<'PY'
from pathlib import Path
import sys

img = Path(sys.argv[1])
with img.open('rb') as f:
    mbr = f.read(512)

entry = mbr[446:462]
start_lba = int.from_bytes(entry[8:12], 'little')
if start_lba == 0:
    raise SystemExit('first partition start LBA is zero')

boot_offset = start_lba * 512
with img.open('rb') as f:
    f.seek(boot_offset)
    boot = f.read(512)

if boot[510:512] != b'\x55\xaa':
    raise SystemExit('invalid FAT boot-sector signature')

if int.from_bytes(boot[11:13], 'little') == 0:
    raise SystemExit('invalid FAT bytes-per-sector')

print(boot_offset)
PY
)"

printf 'Image: %s\n' "$image_path"
printf 'Meta:   %s\n' "$meta_path"
printf 'Size:  %s\n' "$actual_size"
printf 'SHA256: %s\n' "$actual_sha256"
printf 'FAT offset: %s\n' "$partition_offset"

if [ "$actual_size" != "$expected_size" ]; then
	printf 'size mismatch: expected %s\n' "$expected_size" >&2
	exit 1
fi

if [ "$actual_sha256" != "$expected_sha256" ]; then
	printf 'sha256 mismatch: expected %s\n' "$expected_sha256" >&2
	exit 1
fi

mdir -i "${image_path}@@${partition_offset}" :: >/dev/null

printf 'Verification: OK\n'
