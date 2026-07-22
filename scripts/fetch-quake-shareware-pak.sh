#!/usr/bin/env bash
#
# fetch-quake-shareware-pak.sh — download the freely-redistributable Quake
# SHAREWARE pak0.pak into the Pi 4 rootfs overlay so the GLQuake showcase has
# playable game data.
#
# LICENSING: this fetches the QUAKE SHAREWARE episode data, which id Software
# released for free redistribution. It is NOT the full game — the full Quake
# data (id1/pak0.pak + pak1.pak) must be purchased (Steam / GOG / original CD).
# pak0.pak is never committed to this repository (copyright hygiene); it is
# fetched on demand by this script or by the Docker build (PAK0_URL).
#
# The Phoenix-RTOS engine port itself is GPL-2.0-or-later (QuakeSpasm lineage);
# only the game DATA is proprietary-but-shareware.
#
# Usage:
#   scripts/fetch-quake-shareware-pak.sh                 # default public mirror
#   PAK0_URL=<url> scripts/fetch-quake-shareware-pak.sh  # custom source
#   PAK0_DEST=<dir> scripts/fetch-quake-shareware-pak.sh # custom install dir
#
# To use the FULL game instead, copy your own id1/pak0.pak (and pak1.pak) into
# the destination dir shown below and skip this script.
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." && pwd)"

# Public mirror of the Quake shareware pak0 (id Software freely-redistributable
# shareware episode). Overridable via PAK0_URL.
PAK0_URL="${PAK0_URL:-https://archive.org/download/quake-shareware-pak/PAK0.PAK}"
# Canonical shareware pak0 checksum (verified 2026; the archive.org shareware pak).
PAK0_MD5="5906e5998fc3d896ddaf5e6a62e03abb"

DEST="${PAK0_DEST:-$ROOT/sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/rootfs-overlay/usr/share/quake/id1}"
OUT="$DEST/pak0.pak"

echo "== Quake SHAREWARE pak0 fetch =="
echo "   This downloads the FREELY-REDISTRIBUTABLE Quake shareware episode data"
echo "   (not the full game). Source: $PAK0_URL"

if [ -f "$OUT" ]; then
	if command -v md5sum >/dev/null 2>&1 && [ "$(md5sum "$OUT" | cut -d' ' -f1)" = "$PAK0_MD5" ]; then
		echo "   pak0.pak already present and verified ($OUT) — nothing to do."
		exit 0
	fi
	echo "   existing pak0.pak checksum mismatch or unverifiable — re-fetching."
fi

mkdir -p "$DEST"
tmp="$OUT.download"
if command -v wget >/dev/null 2>&1; then
	wget -O "$tmp" "$PAK0_URL"
elif command -v curl >/dev/null 2>&1; then
	curl -fL -o "$tmp" "$PAK0_URL"
else
	echo "ERROR: need wget or curl to fetch pak0.pak" >&2
	exit 1
fi

if command -v md5sum >/dev/null 2>&1; then
	got="$(md5sum "$tmp" | cut -d' ' -f1)"
	if [ "$got" != "$PAK0_MD5" ]; then
		echo "ERROR: pak0.pak checksum mismatch (got $got, want $PAK0_MD5)." >&2
		echo "       Refusing to install a non-canonical file; delete $tmp and retry, or set a trusted PAK0_URL." >&2
		exit 1
	fi
	echo "   checksum OK ($PAK0_MD5)"
else
	echo "   WARNING: md5sum unavailable — installed without checksum verification."
fi

mv -f "$tmp" "$OUT"
echo "   installed -> $OUT"
echo "   (This file is gitignored and will not be committed.)"
