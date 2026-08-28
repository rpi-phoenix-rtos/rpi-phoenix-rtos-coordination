#!/bin/sh
# gen-mp4.sh — regenerate the ISOBMFF (MP4) demux fixtures from the raw .265
# vectors. wp.mp4/dflt.mp4 are single-HEVC-track containers (the demux happy
# path, bit-exact vs the raw goldens); wpaudio.mp4 is a 2-track file (the
# audio/multi-track negative control — hevc-play must REJECT it).
# SPDX-License-Identifier: BSD-3-Clause
set -e
cd "$(dirname "$0")"

# single-track HEVC -> MP4 (hev1, params in hvcC)
ffmpeg -y -loglevel error -f hevc -i wp.265   -c copy wp.mp4
ffmpeg -y -loglevel error -f hevc -i dflt.265 -c copy dflt.mp4

# audio + video (negative control): a silent AAC track alongside the HEVC video
ffmpeg -y -loglevel error -f lavfi -t 1 -i "anullsrc=r=44100:cl=mono" \
       -f hevc -i wp.265 -map 0:a -map 1:v -c:a aac -c:v copy wpaudio.mp4

echo "wrote wp.mp4 dflt.mp4 wpaudio.mp4"
