#!/usr/bin/env bash
#
# Build the standalone Raspberry Pi 4 (BCM2711) HEVC/H.265 decoder M0 bring-up
# smoke test (tools/hevc-probe/hevc-probe.c) with the aarch64-phoenix toolchain.
#
# Produces a single static ELF (hevc-probe). Links the canonical libvcmbox
# client (sources/phoenix-rtos-devices/misc/rpi4-vcmbox/) directly rather than
# copying it — same "reference files outside my dir" pattern wifi-probe uses —
# so there is no third drifting copy of the mailbox library. No
# phoenix-rtos-build tree, no image rebuild; nothing in the default image
# references this binary, so it exists only if you run this script.
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$HERE/../.." && pwd)"

# Toolchain (override with GCC=... / NM=... if it lives elsewhere).
GCC="${GCC:-$REPO_ROOT/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc}"
NM="${NM:-$REPO_ROOT/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-nm}"

# Canonical vcmbox client (single source of truth).
VCMBOX_DIR="${VCMBOX_DIR:-$REPO_ROOT/sources/phoenix-rtos-devices/misc/rpi4-vcmbox}"

OUT="${OUT:-$HERE/hevc-probe}"
CFLAGS="-O2 -static -Wall -Wextra -std=gnu11 -I$VCMBOX_DIR"

for f in "$GCC" "$VCMBOX_DIR/libvcmbox.c" "$VCMBOX_DIR/libvcmbox.h"; do
	if [ ! -e "$f" ]; then
		echo "hevc-probe/build: missing required input: $f" >&2
		exit 1
	fi
done

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "hevc-probe: compiling probe (-O2 -static)"
"$GCC" $CFLAGS -c "$HERE/hevc-probe.c" -o "$TMP/hevc-probe.o"

echo "hevc-probe: compiling libvcmbox client"
"$GCC" $CFLAGS -c "$VCMBOX_DIR/libvcmbox.c" -o "$TMP/libvcmbox.o"

echo "hevc-probe: linking $OUT"
"$GCC" -O2 -static "$TMP/hevc-probe.o" "$TMP/libvcmbox.o" -o "$OUT"

echo "hevc-probe: undefined-symbol check (expect none):"
if "$NM" -u "$OUT" | grep -q .; then
	echo "  !! UNDEFINED SYMBOLS PRESENT:" >&2
	"$NM" -u "$OUT" >&2
	exit 1
fi
echo "  0 undefined symbols."

echo "hevc-probe: build result:"
file "$OUT"
ls -la "$OUT"

# --- staging -----------------------------------------------------------------
# Copy the ELF where the Pi can exec it. Because the netboot NFS /bin is
# repopulated from the image each cycle, a NEW-named binary is also staged at
# the export ROOT (per the netboot-export-drift convention) so it survives.
stage_to() {
	local dir="$1"
	[ -d "$dir" ] || return 0
	if cp "$OUT" "$dir/hevc-probe" 2>/dev/null; then
		echo "  staged -> $dir/hevc-probe"
	fi
}

echo "hevc-probe: staging"
# 1) image filesystem /bin (SD / image builds)
stage_to "$REPO_ROOT/.buildroot/_fs/aarch64a72-generic-rpi4b/root/bin"

# 2) active fsid=0 NFS export (skip commented lines); fall back to the documented default
EXPORT="$(awk '!/^#/ && /fsid=0/{print $1; exit}' /etc/exports 2>/dev/null || true)"
[ -n "${EXPORT:-}" ] || EXPORT="/srv/phoenix-rpi4-nfs"
stage_to "$EXPORT/bin"
stage_to "$EXPORT"

# 3) also the documented default netboot root (if different from the fsid=0 export)
if [ "$EXPORT" != "/srv/phoenix-rpi4-nfs" ]; then
	stage_to "/srv/phoenix-rpi4-nfs/bin"
	stage_to "/srv/phoenix-rpi4-nfs"
fi

echo "hevc-probe: done. Run on the Pi at the psh prompt:  hevc-probe"
