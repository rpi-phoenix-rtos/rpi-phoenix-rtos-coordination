#!/bin/sh
# gen-10bit.sh — regenerate the 10-bit (Main10) regression vector + its golden.
# main10.265 is a 256x256 Main10 clip (I + inter P/B, WPP off) that decodes
# bit-exact on the rpivid HW block. The golden is yuv420p10le — 16-bit LE
# samples RIGHT-ALIGNED (values 0..1023), which is what the HW emits (NOT p010le,
# which left-shifts by 6). NOTE: all-intra 10-bit (x265 keyint=1) selects the
# Range-Extensions "Rext" profile (extra out-of-subset tools) — force inter GOP
# (default) or `-profile:v main10` to stay in the decodable Main10 subset.
# SPDX-License-Identifier: BSD-3-Clause
set -e
cd "$(dirname "$0")"
ffmpeg -y -loglevel error -f lavfi -i "testsrc2=size=256x256:rate=25:duration=0.4" \
       -c:v libx265 -pix_fmt yuv420p10le -x265-params "wpp=0:log-level=none" main10.265
ffmpeg -y -loglevel error -i main10.265 -f rawvideo -pix_fmt yuv420p10le main10.yuv10
echo "wrote main10.265 main10.yuv10"
