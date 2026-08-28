#!/bin/sh
# gen-mp4.sh — regenerate the ISOBMFF (MP4) demux fixtures from the raw .265
# vectors. wp.mp4/dflt.mp4 are single-HEVC-track containers; wpaudio.mp4 and
# hd720aud.mp4 are audio+video files (the demux must SKIP the audio track and
# extract the video via its sample tables). All four decode bit-exact vs the
# raw-.265 goldens (wpaudio/hd720aud reuse wp.nv12 / hd720.nv12 from gen-hd.sh).
# SPDX-License-Identifier: BSD-3-Clause
set -e
cd "$(dirname "$0")"

# single-track HEVC -> MP4 (hev1, params in hvcC)
ffmpeg -y -loglevel error -f hevc -i wp.265   -c copy wp.mp4
ffmpeg -y -loglevel error -f hevc -i dflt.265 -c copy dflt.mp4

# audio + video: a real AAC track interleaved with the HEVC video (exercises the
# stsc/stco/stsz sample-table path — audio chunks must be skipped by offset)
ffmpeg -y -loglevel error -f lavfi -t 1 -i "anullsrc=r=44100:cl=mono" \
       -f hevc -i wp.265 -map 0:a -map 1:v -c:a aac -c:v copy wpaudio.mp4
ffmpeg -y -loglevel error -f lavfi -t 0.6 -i "sine=frequency=440:r=44100" \
       -i hd720.265 -map 1:v -map 0:a -c:v copy -c:a aac -f mp4 hd720aud.mp4

echo "wrote wp.mp4 dflt.mp4 wpaudio.mp4 hd720aud.mp4"
