#!/usr/bin/env bash
# Build e4-play — the fb-direct (no-X11) H.264 player: decodes a raw Annex-B clip
# with the Phoenix ffmpeg decode-core archives and presents each frame centered on
# /dev/fb0 (HDMI) with wall-clock pacing. (X11 variant: build-e4-x11-play.sh.)
# Pair with tools/ram-stage/ram-stage-play to play a BIGGER clip from a RAM disk:
#   ram-stage-play /usr/share/e4 /ramtmp/e4 /bin/e4-play /ramtmp/e4/<clip>.h264
# Bigger test clips (host, needs ffmpeg+libx264), baseline Annex-B (what e4_play wants):
#   ffmpeg -f lavfi -i testsrc2=size=1280x720:rate=12:duration=10 \
#     -c:v libx264 -profile:v baseline -pix_fmt yuv420p -g 6 -bf 0 -f h264 big720.h264
set -euo pipefail
ROOT=/home/houp/phoenix-rpi
TC="$ROOT/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc"
FF="$ROOT/external/ffmpeg"
LP="$ROOT/.buildroot/_build/aarch64a72-generic-rpi4b/lib/libphoenix.a"
OUT="${1:-/tmp/e4-play}"
"$TC" -c -O2 -g -I "$FF" -o /tmp/e4_play.o "$ROOT/tools/ffmpeg-port/e4_play.c"
"$TC" -o "$OUT" /tmp/e4_play.o -Wl,--start-group \
	"$FF/libavformat/libavformat.a" "$FF/libavcodec/libavcodec.a" "$FF/libavutil/libavutil.a" "$LP" \
	-Wl,--end-group -lm -lgcc
echo "built $OUT ($(stat -c%s "$OUT") bytes)"
