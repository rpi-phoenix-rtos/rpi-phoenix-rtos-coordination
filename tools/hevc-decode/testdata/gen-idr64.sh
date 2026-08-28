#!/usr/bin/env bash
#
# Regenerate the HEVC M2 test vector: one IDR frame, 64x64, 8-bit, 4:2:0,
# single-tile, no WPP, no temporal-MVP, no SAO — the minimal decode for rpivid
# bring-up. Requires a host ffmpeg built with libx265.
#
# NOTE: x265 output varies by version — the committed idr64.265 is the CANONICAL
# vector the M2 harness's hardcoded field offsets match. Only regenerate if you
# also re-extract data_byte_offset/bit_size and update the harness + README.
#
# SPDX-License-Identifier: BSD-3-Clause
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

ffmpeg -hide_banner -loglevel error -f lavfi -i color=c=gray:s=64x64:d=1 -frames:v 1 \
	-c:v libx265 \
	-x265-params "log-level=none:no-temporal-mvp=1:sao=0:deblock=-6,-6:no-open-gop=1:keyint=1" \
	-y "$HERE/idr64.265"

echo "wrote $HERE/idr64.265 ($(stat -c%s "$HERE/idr64.265") bytes)"
echo "verify fields:  ffmpeg -v trace -i idr64.265 -c copy -bsf:v trace_headers -f null -"
