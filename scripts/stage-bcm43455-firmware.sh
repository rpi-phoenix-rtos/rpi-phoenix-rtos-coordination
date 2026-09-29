#!/usr/bin/env bash
#
# Stage the BCM43455 firmware blobs into .firmware/ (gitignored) under the flat
# names the standalone WiFi probe's generators expect (scripts/gen-wifi-fw-c.sh,
# tools/wifi-probe/gen-clm.py):
#
#   .firmware/brcmfmac43455-sdio.bin       (643651 B, main firmware)
#   .firmware/brcmfmac43455-sdio.clm_blob  (4733 B, country / regulatory)
#   .firmware/brcmfmac43455-sdio.txt       (1883 B, Pi 4 NVRAM)
#
# The image does NOT use these: rpi4-wifi reads /lib/firmware/brcm/, staged by
# scripts/fetch-wifi-firmware.sh. This script takes the same pinned,
# sha256-verified linux-firmware files from that script's cache, so the probe and
# the image run identical firmware. The blobs are under the Cypress licence (the
# NVRAM under GPL-2.0); see docs/misc/2026-09-30-wifi-in-image.md.
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

set -euo pipefail

repo="${PHOENIX_RPI_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}"
out_dir="$repo/.firmware"
lf_tag="$(sed -n 's/^LF_TAG=//p' "$repo/scripts/fetch-wifi-firmware.sh")"
cache="${WIFI_FW_CACHE:-$out_dir/linux-firmware-$lf_tag}"

mkdir -p "$out_dir"
WIFI_FW_CACHE="$cache" "$repo/scripts/fetch-wifi-firmware.sh" --cache-only

for pair in \
	"cypress/cyfmac43455-sdio.bin|brcmfmac43455-sdio.bin" \
	"cypress/cyfmac43455-sdio.clm_blob|brcmfmac43455-sdio.clm_blob" \
	"brcm/brcmfmac43455-sdio.raspberrypi,4-model-b.txt|brcmfmac43455-sdio.txt"; do
	src="${pair%%|*}"
	dst="${pair##*|}"
	if [ ! -f "$cache/$src" ]; then
		printf 'ERROR: %s is not in the firmware cache (no network?)\n' "$src" >&2
		exit 1
	fi
	cp -f "$cache/$src" "$out_dir/$dst"
	printf '[stage-fw] %s <- %s\n' "$dst" "$src"
done
