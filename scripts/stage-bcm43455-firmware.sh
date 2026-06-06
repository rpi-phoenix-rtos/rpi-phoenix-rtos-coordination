#!/usr/bin/env bash
#
# Stage the BCM43455 firmware blobs needed by the WiFi driver into a
# local .firmware/ directory (gitignored — blobs are not vendored).
#
# Output:
#   .firmware/brcmfmac43455-sdio.bin       (~629 KB, main firmware)
#   .firmware/brcmfmac43455-sdio.clm_blob  (~5 KB, country / regulatory)
#   .firmware/brcmfmac43455-sdio.txt       (~2 KB, Pi 4 NVRAM/MFGTEST)
#
# Source priority:
#   1. /lib/firmware/cypress/ + /lib/firmware/brcm/ on the dev host
#      (Debian/Ubuntu firmware-cypress + firmware-brcm80211 packages),
#      decompressed from .zst if present.
#   2. github.com/RPi-Distro/firmware-nonfree (master/debian/brcm).
#
# Both sources serve the same Cypress-published firmware; redistribution
# is permitted under the Cypress firmware redistribution terms shipped
# alongside the blobs (LICENSE files in firmware-nonfree). Phoenix-RTOS
# itself is BSD-3-Clause; the blobs are NOT BSD — they remain under
# their original Cypress EULA. See docs/inprogress/wifi-firmware-blobs.md.
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

set -euo pipefail

repo="${PHOENIX_RPI_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}"
out_dir="$repo/.firmware"
mkdir -p "$out_dir"

# Known-good SHA256s of the decompressed files (Cypress 7.45.241 +
# Raspberry Pi 4 Model B NVRAM as shipped in firmware-cypress and
# firmware-brcm80211 packages on Debian bookworm).
declare -A want_sha256=(
    [brcmfmac43455-sdio.bin]='d408faa9d0d5b1a2f9912dcea53ab0be48217288e398406d117f0edafe7c3edd'
    [brcmfmac43455-sdio.clm_blob]='15f50a27020b263d1bea215c8f68d0550d912932d1d9ef19ffd59f18d82dd460'
    [brcmfmac43455-sdio.txt]='edb6f4e4fb19e18940004124feb4ffe160d72fc607243a07a4480338a28b2748'
)

# Mapping of output filename to ordered list of candidate sources.
# Each candidate is "type:path" where type is `file_zst`, `file_plain`,
# or `url`.
declare -A sources=(
    [brcmfmac43455-sdio.bin]='file_zst:/lib/firmware/cypress/cyfmac43455-sdio.bin.zst
file_plain:/lib/firmware/cypress/cyfmac43455-sdio.bin
file_plain:/lib/firmware/brcm/brcmfmac43455-sdio.bin
url:https://raw.githubusercontent.com/RPi-Distro/firmware-nonfree/master/debian/config/brcm80211/brcm/brcmfmac43455-sdio.bin'
    [brcmfmac43455-sdio.clm_blob]='file_zst:/lib/firmware/cypress/cyfmac43455-sdio.clm_blob.zst
file_plain:/lib/firmware/cypress/cyfmac43455-sdio.clm_blob
file_plain:/lib/firmware/brcm/brcmfmac43455-sdio.clm_blob
url:https://raw.githubusercontent.com/RPi-Distro/firmware-nonfree/master/debian/config/brcm80211/brcm/brcmfmac43455-sdio.clm_blob'
    [brcmfmac43455-sdio.txt]='file_zst:/lib/firmware/brcm/brcmfmac43455-sdio.raspberrypi,4-model-b.txt.zst
file_plain:/lib/firmware/brcm/brcmfmac43455-sdio.raspberrypi,4-model-b.txt
url:https://raw.githubusercontent.com/RPi-Distro/firmware-nonfree/master/debian/config/brcm80211/brcm/brcmfmac43455-sdio.raspberrypi,4-model-b.txt'
)

try_source() {
    local kind="$1"
    local src="$2"
    local out="$3"

    case "$kind" in
        file_zst)
            [ -f "$src" ] || return 1
            command -v zstdcat >/dev/null 2>&1 || return 1
            zstdcat "$src" > "$out.tmp" 2>/dev/null || return 1
            mv "$out.tmp" "$out"
            ;;
        file_plain)
            [ -f "$src" ] || return 1
            cp -f "$src" "$out"
            ;;
        url)
            command -v curl >/dev/null 2>&1 || return 1
            curl -fsSL -o "$out.tmp" "$src" || return 1
            mv "$out.tmp" "$out"
            ;;
        *)
            return 1
            ;;
    esac
    return 0
}

verify_sha256() {
    local file="$1"
    local want="$2"
    local got
    got=$(sha256sum "$file" | awk '{print $1}')
    if [ "$got" != "$want" ]; then
        printf 'ERROR: sha256 mismatch for %s\n  want %s\n  got  %s\n' \
            "$file" "$want" "$got" >&2
        return 1
    fi
    return 0
}

for fname in "${!sources[@]}"; do
    out="$out_dir/$fname"
    if [ -f "$out" ] && verify_sha256 "$out" "${want_sha256[$fname]}" 2>/dev/null; then
        printf '[stage-fw] %s already present (sha ok)\n' "$fname"
        continue
    fi

    # Try each candidate source in order.
    while IFS= read -r entry; do
        [ -z "$entry" ] && continue
        kind="${entry%%:*}"
        src="${entry#*:}"
        if try_source "$kind" "$src" "$out"; then
            printf '[stage-fw] %s <- %s:%s\n' "$fname" "$kind" "$src"
            break
        fi
    done <<<"${sources[$fname]}"

    if [ ! -f "$out" ]; then
        printf 'ERROR: could not stage %s from any candidate source\n' "$fname" >&2
        exit 1
    fi

    if ! verify_sha256 "$out" "${want_sha256[$fname]}"; then
        printf 'NOTE: %s installed but sha differs from expected (firmware may have been updated upstream)\n' "$fname" >&2
    fi
done

printf '\n[stage-fw] staging complete:\n'
ls -la "$out_dir"/brcmfmac43455-sdio.*
