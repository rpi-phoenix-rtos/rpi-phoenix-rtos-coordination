#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." && pwd)"
image_path="${RPI4B_SDIMG_PATH:-$repo_root/artifacts/rpi4b/rpi4b-sd.img}"
report_dir="${RPI4B_REPORT_DIR:-$repo_root/artifacts/rpi4b-reports}"
timestamp="$(date +%Y%m%d-%H%M%S)"
report_path="${RPI4B_REPORT_PATH:-$report_dir/pi4-first-trial-$timestamp.md}"

if [ ! -f "$image_path" ]; then
	printf 'missing image: %s\n' "$image_path" >&2
	exit 1
fi

image_sha256="${RPI4B_SDIMG_SHA256:-$(shasum -a 256 "$image_path" | awk '{print $1}')}"

mkdir -p "$report_dir"

cat > "$report_path" <<EOF
# Pi 4 First Hardware Trial Report

- Image: $image_path
- SHA-256: $image_sha256

## Hardware

- Board revision:
- Display:
- Keyboard:
- Ethernet attached: yes/no
- USB-TTL adapter:
- Serial device path:
- BOOT_UART enabled in EEPROM: yes/no/unknown

## Observed Class

- firmware-load / early-boot / runtime-no-input / runtime-shell / reboot-loop / unknown

## Timing

- Power-on time observed:

## HDMI Result

- no signal / brief flash / stable picture
- early panel seen: yes/no
- black text console seen: yes/no
- prompt seen: yes/no

## Keyboard Result

- no visible effect / partial / full
- keys tried:

## UART Result

- UART connected: yes/no
- capture log path:
- earliest visible output:
- latest visible output:
- summary helper result:

## Command Results

- help:
- ps:
- ls /:

## LED / Reboot Behavior

-

## Additional Notes

-
EOF

printf 'Created: %s\n' "$report_path"
