#!/usr/bin/env bash
#
# Build the Raspberry Pi 4 (BCM2711) HEVC/H.265 decoder M1 bring-up harness
# (tools/hevc-decode/hevc-m1.c) with the aarch64-phoenix toolchain.
#
# Produces a single static ELF (hevc-m1). Links the canonical libvcmbox client
# (sources/phoenix-rtos-devices/misc/rpi4-vcmbox/) directly — same pattern as
# tools/hevc-probe/build-hevc-probe.sh, so there is no drifting mailbox copy.
# No phoenix-rtos-build tree, no image rebuild; nothing in the default image
# references this binary.
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$HERE/../.." && pwd)"

GCC="${GCC:-$REPO_ROOT/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc}"
VCMBOX_DIR="${VCMBOX_DIR:-$REPO_ROOT/sources/phoenix-rtos-devices/misc/rpi4-vcmbox}"

OUT="${OUT:-$HERE/hevc-m1}"
CFLAGS="-O2 -static -Wall -Wextra -std=gnu11 -I$VCMBOX_DIR"

for f in "$GCC" "$VCMBOX_DIR/libvcmbox.c" "$VCMBOX_DIR/libvcmbox.h"; do
	if [ ! -e "$f" ]; then
		echo "hevc-m1/build: missing required input: $f" >&2
		exit 1
	fi
done

echo "hevc-m1: compiling M1 harness (-O2 -static)"
"$GCC" $CFLAGS -o "$OUT" "$HERE/hevc-m1.c" "$VCMBOX_DIR/libvcmbox.c"
echo "hevc-m1: built $OUT"
file "$OUT" 2>/dev/null || true
