#!/usr/bin/env bash
#
# Build and stage `hevc-play` — the runtime .265/.mp4 player that decodes H.265 on
# the BCM2711's rpivid hardware block and displays to /dev/fb0.
#
# Sibling of build-hevc-m2.sh, which builds the M2 *conformance harness* (fixed
# clip compiled in). This builds the PLAYER: same decode core plus hevc_parse.c
# and -DPLAY_TOOL, so it takes a file path at runtime. Recipe is the one in
# README.md ("Build + run").
#
# Wanted for the showcase reel: hardware video decode is a capability the port has
# and the recording had no segment for it. Neither the player nor a clip was in
# the demo image, so this also stages both.
#
#   ./tools/hevc-decode/build-hevc-play.sh [clip.265]
#
# Stages bin/hevc-play plus the clip into the build tree AND the live NFS export,
# so a netboot cycle picks them up (the cycle re-syncs /bin from .buildroot/_fs,
# so staging to only the export would be overwritten -- see
# docs/knowledge notes on the fsid=0 export).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$HERE/../.." && pwd)"

GCC="${GCC:-$REPO_ROOT/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc}"
VCMBOX_DIR="${VCMBOX_DIR:-$REPO_ROOT/sources/phoenix-rtos-devices/misc/rpi4-vcmbox}"
TARGET="${RPI4B_TARGET:-aarch64a72-generic-rpi4b}"
FS_ROOT="${REPO_ROOT}/.buildroot/_fs/${TARGET}/root"
CLIP="${1:-$HERE/testdata/hd1080b.265}"

OUT="${OUT:-$HERE/hevc-play}"
CFLAGS="-O2 -static -Wall -Wextra -std=gnu11 -DPLAY_TOOL -I$VCMBOX_DIR -I$HERE"

for f in "$GCC" "$VCMBOX_DIR/libvcmbox.c" "$HERE/hevc-m2.c" "$HERE/hevc_parse.c" "$HERE/hevc_mp4.c" "$CLIP"; do
	[ -e "$f" ] || { echo "hevc-play/build: missing required input: $f" >&2; exit 1; }
done

echo "hevc-play: compiling runtime player (-O2 -static -DPLAY_TOOL)"
"$GCC" $CFLAGS -o "$OUT" "$HERE/hevc-m2.c" "$HERE/hevc_parse.c" "$HERE/hevc_mp4.c" "$VCMBOX_DIR/libvcmbox.c"

# Phoenix has no dynamic loader for ordinary programs; a PT_INTERP here would be a
# binary that cannot start at all, so check rather than trust.
if readelf -l "$OUT" 2>/dev/null | grep -q INTERP; then
	echo "hevc-play/build: $OUT has a PT_INTERP segment (not static)" >&2
	exit 1
fi
echo "hevc-play: built $OUT ($(stat -c%s "$OUT") bytes)"

clip_base="$(basename "$CLIP")"
for dst in "$FS_ROOT" "$(awk '$0 ~ /fsid=0/ && $1 ~ /^\//{print $1; exit}' /etc/exports /etc/exports.d/*.exports 2>/dev/null)"; do
	[ -n "$dst" ] && [ -d "$dst" ] || continue
	install -Dm755 "$OUT" "$dst/bin/hevc-play" 2>/dev/null \
		|| sudo -n install -Dm755 "$OUT" "$dst/bin/hevc-play"
	install -Dm644 "$CLIP" "$dst/usr/share/demo/$clip_base" 2>/dev/null \
		|| sudo -n install -Dm644 "$CLIP" "$dst/usr/share/demo/$clip_base"
	echo "hevc-play: staged -> $dst/bin/hevc-play + usr/share/demo/$clip_base"
done

echo "hevc-play: run on the Pi with:  hevc-play /usr/share/demo/$clip_base"
