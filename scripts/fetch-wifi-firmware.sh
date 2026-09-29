#!/usr/bin/env bash
#
# fetch-wifi-firmware.sh -- fetch the Raspberry Pi 4 WiFi (BCM43455) firmware
# from linux-firmware, pinned by commit and sha256, and stage it into the rpi4b
# rootfs overlay as /lib/firmware, where the rpi4-wifi daemon reads it.
#
#   /lib/firmware/brcm/brcmfmac43455-sdio.bin                        firmware (Cypress licence)
#   /lib/firmware/brcm/brcmfmac43455-sdio.clm_blob                   regulatory data (Cypress licence)
#   /lib/firmware/brcm/brcmfmac43455-sdio.raspberrypi,4-model-b.txt  board NVRAM (GPL-2.0)
#   /lib/firmware/LICENSES/LICENCE.cypress, /lib/firmware/LICENSES/GPL-2.0
#   /lib/firmware/WHENCE                                             where each file came from
#
# The files are NOT in any git repository. They are downloaded once into a cache
# (.firmware/linux-firmware-<tag>/, gitignored) and every later build is offline.
#
# Outcomes (exit status):
#   0  staged and verified -- or no network and nothing cached: then a WARNING is
#      printed, nothing is staged, and the image builds without WiFi (the daemon
#      logs "WiFi disabled" at boot)
#   1  a sha256 mismatch: a download that does not match is never staged, and a
#      corrupt cached copy is re-fetched (and fails if it cannot be). A staged copy
#      that does not match is replaced from the verified cache.
#   2  usage error
#
# Why this commit and not linux-firmware HEAD: HEAD (a137a09, 2026-08-31) moved the
# 43455 firmware from 7.45.234 to 7.45.286, a build WITHOUT the in-firmware
# supplicant ("idsup"). rpi4-wifi hands the firmware the passphrase (sup_wpa +
# WLC_SET_WSEC_PMK) and lets it run the WPA2 4-way handshake, so it needs 7.45.234,
# the firmware every Pi WiFi test so far has run. Tag 20260810 is the last release
# carrying it; its three files are byte-identical to that tested set.
#
# Usage:
#   scripts/fetch-wifi-firmware.sh              fetch (if needed) + stage into the overlay
#   scripts/fetch-wifi-firmware.sh --cache-only fetch (if needed) + verify, stage nothing
#   scripts/fetch-wifi-firmware.sh --remove     delete the staged copy (build without WiFi)
#
# Env overrides:
#   WIFI_FW_CACHE   cache dir   (default: .firmware/linux-firmware-<tag>)
#   WIFI_FW_DEST    staging dir (default: the rpi4b project's rootfs-overlay/lib/firmware)
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

LF_TAG=20260810
LF_COMMIT=2135b2f7714a3a514c989b9728f51f36144cab6f
LF_MIRRORS=(
	"https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain/@PATH@?id=${LF_COMMIT}"
	"https://gitlab.com/kernel-firmware/linux-firmware/-/raw/${LF_COMMIT}/@PATH@"
)

# "<path in linux-firmware>|<path under /lib/firmware>|<sha256>"
FILES=(
	"cypress/cyfmac43455-sdio.bin|brcm/brcmfmac43455-sdio.bin|d408faa9d0d5b1a2f9912dcea53ab0be48217288e398406d117f0edafe7c3edd"
	"cypress/cyfmac43455-sdio.clm_blob|brcm/brcmfmac43455-sdio.clm_blob|15f50a27020b263d1bea215c8f68d0550d912932d1d9ef19ffd59f18d82dd460"
	"brcm/brcmfmac43455-sdio.raspberrypi,4-model-b.txt|brcm/brcmfmac43455-sdio.raspberrypi,4-model-b.txt|edb6f4e4fb19e18940004124feb4ffe160d72fc607243a07a4480338a28b2748"
	"LICENSES/LICENCE.cypress|LICENSES/LICENCE.cypress|ae0db6cc4db33941148df0f67de53e76a77b1b5a46b3165edb7040aa2750015f"
	"LICENSES/GPL-2.0|LICENSES/GPL-2.0|edaef632cbb643e4e7a221717a6c441a4c1a7c918e6e4d56debc3d8739b233f6"
)

CACHE="${WIFI_FW_CACHE:-${repo_root}/.firmware/linux-firmware-${LF_TAG}}"
DEST="${WIFI_FW_DEST:-${repo_root}/sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/rootfs-overlay/lib/firmware}"

log()  { printf '[wifi-fw] %s\n' "$*"; }
warn() { printf '[wifi-fw] WARNING: %s\n' "$*" >&2; }
fail() { printf '[wifi-fw] ERROR: %s\n' "$*" >&2; exit 1; }

sha_of() { sha256sum "$1" | cut -d' ' -f1; }

mode=stage
case "${1:-}" in
	"") ;;
	--cache-only) mode=cache ;;
	--remove) mode=remove ;;
	-h|--help) sed -n '2,45p' "$0"; exit 0 ;;
	*) printf 'usage: %s [--cache-only|--remove]\n' "$0" >&2; exit 2 ;;
esac

if [ "${mode}" = remove ]; then
	for entry in "${FILES[@]}"; do
		IFS='|' read -r _ dst _ <<<"${entry}"
		rm -f "${DEST}/${dst}"
	done
	rm -f "${DEST}/WHENCE"
	log "removed the staged WiFi firmware from ${DEST} (the next image has no WiFi)"
	exit 0
fi

# Download one file into the cache, trying each mirror. 0 = downloaded (not yet
# verified), 1 = no mirror reachable.
download() {
	local src="$1" out="$2" url mirror
	for mirror in "${LF_MIRRORS[@]}"; do
		url="${mirror//@PATH@/${src}}"
		if command -v curl >/dev/null 2>&1; then
			curl -fsSL --connect-timeout 15 --retry 2 -o "${out}.part" "${url}" 2>/dev/null || { rm -f "${out}.part"; continue; }
		elif command -v wget >/dev/null 2>&1; then
			wget -q -T 15 -O "${out}.part" "${url}" || { rm -f "${out}.part"; continue; }
		else
			return 1
		fi
		mv -f "${out}.part" "${out}"
		log "downloaded ${src} (${url%%\?*})"
		return 0
	done
	return 1
}

# 1. Make the cache complete and verified.
mkdir -p "${CACHE}"
missing=()
for entry in "${FILES[@]}"; do
	IFS='|' read -r src _ want <<<"${entry}"
	cached="${CACHE}/${src}"
	if [ -f "${cached}" ]; then
		got="$(sha_of "${cached}")"
		[ "${got}" = "${want}" ] && continue
		# A cached copy that does not match is never used, and never silently
		# replaced by a guess: re-fetch it, and fail below if that also mismatches.
		warn "cached ${src} has sha256 ${got}, want ${want}; re-fetching"
		mv -f "${cached}" "${cached}.bad"
	fi
	mkdir -p "$(dirname "${cached}")"
	if ! download "${src}" "${cached}"; then
		missing+=("${src}")
		continue
	fi
	got="$(sha_of "${cached}")"
	if [ "${got}" != "${want}" ]; then
		mv -f "${cached}" "${cached}.bad"
		fail "sha256 mismatch for ${src} from linux-firmware ${LF_COMMIT}: got ${got}, want ${want} (kept as ${cached}.bad)"
	fi
	rm -f "${cached}.bad"
done

staged_ok() {
	local entry dst want
	for entry in "${FILES[@]}"; do
		IFS='|' read -r _ dst want <<<"${entry}"
		[ -f "${DEST}/${dst}" ] && [ "$(sha_of "${DEST}/${dst}")" = "${want}" ] || return 1
	done
	return 0
}

if [ "${#missing[@]}" -gt 0 ]; then
	for src in "${missing[@]}"; do
		[ -f "${CACHE}/${src}.bad" ] && fail "${src}: the cached copy does not match its sha256 and no mirror is reachable to re-fetch it"
	done
	if [ "${mode}" = stage ] && staged_ok; then
		log "no network and the cache is incomplete, but the staged copy in ${DEST} is complete and verified; keeping it"
		exit 0
	fi
	warn "no network and no cached copy of: ${missing[*]}"
	warn "the image will be built WITHOUT WiFi firmware (rpi4-wifi logs \"WiFi disabled\" at boot)."
	warn "Re-run with network access once; the download is cached in ${CACHE}."
	exit 0
fi
log "cache verified: linux-firmware ${LF_TAG} (${LF_COMMIT}) in ${CACHE}"
[ "${mode}" = cache ] && exit 0

# 2. Stage into the overlay (copied into the rootfs by the build's `fs` stage).
changed=0
for entry in "${FILES[@]}"; do
	IFS='|' read -r src dst want <<<"${entry}"
	if [ -f "${DEST}/${dst}" ] && [ "$(sha_of "${DEST}/${dst}")" = "${want}" ]; then
		continue
	fi
	mkdir -p "$(dirname "${DEST}/${dst}")"
	cp -f "${CACHE}/${src}" "${DEST}/${dst}.part"
	chmod 0644 "${DEST}/${dst}.part"
	mv -f "${DEST}/${dst}.part" "${DEST}/${dst}"
	[ "$(sha_of "${DEST}/${dst}")" = "${want}" ] || fail "staged ${DEST}/${dst} does not match its sha256"
	changed=1
done

whence="${DEST}/WHENCE"
whence_new="${whence}.part"
{
	printf 'WiFi firmware for the Raspberry Pi 4 (Broadcom/Cypress BCM43455), used by\n'
	printf 'rpi4-wifi. Unmodified copies of files from linux-firmware:\n\n'
	printf '  https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git\n'
	printf '  tag %s, commit %s\n\n' "${LF_TAG}" "${LF_COMMIT}"
	printf '%-50s  %-50s  %s\n' "file here" "file in linux-firmware" "sha256"
	for entry in "${FILES[@]}"; do
		IFS='|' read -r src dst want <<<"${entry}"
		printf '%-50s  %-50s  %s\n' "${dst}" "${src}" "${want}"
	done
	cat <<'EOF'

Licences, as the linux-firmware WHENCE file gives them:

  cypress/cyfmac43455-sdio.bin, cypress/cyfmac43455-sdio.clm_blob
    (installed under their brcm/brcmfmac43455-sdio.* link names)
    Licence: Redistributable. See LICENCE.cypress for details.
    -> LICENSES/LICENCE.cypress

  brcm/brcmfmac43455-sdio.raspberrypi,4-model-b.txt
    Licence: GPLv2. See GPL-2.0 for details.
    -> LICENSES/GPL-2.0 (this text file is its own source)

The firmware may be reproduced and distributed in object code form only, solely
for use with Cypress integrated circuit products, and may not be modified; see
LICENSES/LICENCE.cypress for the full terms.
EOF
} > "${whence_new}"
if [ -f "${whence}" ] && cmp -s "${whence}" "${whence_new}"; then
	rm -f "${whence_new}"
else
	chmod 0644 "${whence_new}"
	mv -f "${whence_new}" "${whence}"
	changed=1
fi

if [ "${changed}" = 1 ]; then
	log "staged into ${DEST} (lib/firmware of the rootfs overlay)"
else
	log "already staged in ${DEST}"
fi
