#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
#
# CACHE-COLD host-side verification of the Pi's SD scratch write. Regenerates
# the sdtest pattern and compares it against the raw card scratch region read
# directly on the host (bypasses any Pi-side cache — the definitive check for a
# DMA-write coherency bug). Run AFTER the Pi has written the scratch region and
# the card has been swapped back into the host reader.
#
#   sudo ./verify-host.sh [/dev/sda]
set -euo pipefail
DEV="${1:-/dev/sda}"
OFF_MIB=512
BYTES=$((16 * 1024 * 1024))
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

python3 - "$TMP/expected.bin" "$BYTES" <<'PY'
import sys, struct
out, n = sys.argv[1], int(sys.argv[2])
ba = bytearray(n)
for k in range(n // 4):
    struct.pack_into('<I', ba, k * 4, (k ^ 0xA5A5A5A5) & 0xffffffff)
open(out, 'wb').write(ba)
PY

dd if="$DEV" of="$TMP/actual.bin" bs=1M skip="$OFF_MIB" count=16 2>/dev/null

if cmp -s "$TMP/expected.bin" "$TMP/actual.bin"; then
	echo "SDTEST-HOST-VERIFY PASS — cache-cold on-disk bytes match the pattern (no DMA-write corruption)"
else
	echo "SDTEST-HOST-VERIFY FAIL — on-disk bytes differ from the written pattern:"
	cmp "$TMP/expected.bin" "$TMP/actual.bin" | head
fi
