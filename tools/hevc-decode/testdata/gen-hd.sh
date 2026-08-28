#!/bin/sh
# gen-hd.sh — regenerate the HD default-x265 regression vectors + their NV12
# goldens. These prove the decoder handles real HD content (b-pyramid +
# multi-ref + temporal-MVP + WPP, hundreds of CTBs) bit-exact — 720p (240 CTBs)
# and 1080p (510 CTBs). Default x265 (WPP auto-on >256px wide), synthetic
# testsrc2 content. The .265 vectors are committed; the .nv12 goldens are large
# (20–31 MB) and regenerated here for `hevc-play <clip> <golden>` --verify.
# SPDX-License-Identifier: BSD-3-Clause
set -e
cd "$(dirname "$0")"

ffmpeg -y -loglevel error -f lavfi -i "testsrc2=size=1280x720:rate=25:duration=0.6" \
       -c:v libx265 -x265-params "wpp=1:log-level=none" -pix_fmt yuv420p hd720.265
ffmpeg -y -loglevel error -i hd720.265 -f rawvideo -pix_fmt nv12 hd720.nv12

ffmpeg -y -loglevel error -f lavfi -i "testsrc2=size=1920x1080:rate=25:duration=0.4" \
       -c:v libx265 -x265-params "wpp=1:log-level=none" -pix_fmt yuv420p hd1080b.265
ffmpeg -y -loglevel error -i hd1080b.265 -f rawvideo -pix_fmt nv12 hd1080b.nv12

echo "wrote hd720.{265,nv12} hd1080b.{265,nv12}"
