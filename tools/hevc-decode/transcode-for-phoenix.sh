#!/usr/bin/env bash
#
# Re-encode any video the owner hands over (an iPhone clip, say) into the exact
# H.265 subset the Pi 4's rpivid hardware decoder is verified bit-exact on, and
# stage it for playback on the Pi.
#
# Why re-encode instead of stream-copying an iPhone .mov (which is ALREADY HEVC):
# our decoder covers the tools x265 enables by default and rejects or mismatches
# on the rest (tiles, non-zero deblock offsets, AMP, emulation-prevention bytes
# inside headers, the Range-Extensions intra profile). Apple's encoder is free to
# use any of those, and there is no way to know from the outside which it did. A
# re-encode through the parameter set our conformance harness actually exercises
# is deterministic; a stream copy is a coin flip.
#
# Also handled here because they bite specifically on phone footage:
#   - Rotation. Phones record landscape sensor data plus a rotation matrix.
#     ffmpeg applies it on decode by default (-autorotate), so the output is
#     upright and the decoder never sees a display matrix it does not parse.
#   - Frame rate. 30/60 fps is fine; a 240 fps slow-mo clip is decimated, since
#     hevc-play presents as fast as it decodes and has no clock.
#   - Chroma/bit depth. Forced to yuv420p (Main) -- the decoder is 4:2:0 only.
#
# NOT handled: HDR. A Dolby Vision / HLG clip is tone-mapped only approximately
# by the colour flags below; if the result looks washed out, that is why. Ask for
# a clip recorded with HDR off, or accept the look.
#
# Usage:
#   ./tools/hevc-decode/transcode-for-phoenix.sh <input> [output.265] [options]
#     --height N     scale to N lines, keeping aspect (default 1080; 0 = keep)
#     --fps N        cap frame rate (default 30; 0 = keep)
#     --secs N       take only the first N seconds (default: whole clip)
#     --10bit        encode Main10 instead of 8-bit Main (both are verified)
#     --no-stage     do not copy into the netboot root
#
# Output: an Annex-B elementary stream. hevc-play reads .265 directly and also
# demuxes .mp4/.mov, but the elementary stream keeps one fewer thing in the way.

set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

in=""
out=""
height=1080
fps=30
secs=""
depth=8
stage=1

while [ $# -gt 0 ]; do
	case "$1" in
	--height) height="${2:?}"; shift 2 ;;
	--fps) fps="${2:?}"; shift 2 ;;
	--secs) secs="${2:?}"; shift 2 ;;
	--10bit) depth=10; shift ;;
	--no-stage) stage=0; shift ;;
	-h | --help) sed -n '2,40p' "${BASH_SOURCE[0]}"; exit 0 ;;
	-*) echo "unknown option: $1" >&2; exit 2 ;;
	*) if [ -z "$in" ]; then in="$1"; elif [ -z "$out" ]; then out="$1"; else
		echo "unexpected argument: $1" >&2; exit 2; fi; shift ;;
	esac
done

[ -n "$in" ] || { echo "usage: $(basename "$0") <input> [output.265] [options]" >&2; exit 2; }
[ -f "$in" ] || { echo "no such file: $in" >&2; exit 1; }
command -v ffmpeg >/dev/null || { echo "ffmpeg not found" >&2; exit 1; }

if [ -z "$out" ]; then
	base="$(basename "${in%.*}")"
	out="$repo/tools/hevc-decode/testdata/${base}-phoenix.265"
fi
mkdir -p "$(dirname "$out")"

echo "=== source ==="
ffprobe -v error -select_streams v:0 \
	-show_entries stream=codec_name,width,height,pix_fmt,r_frame_rate,color_transfer \
	-show_entries stream_side_data=rotation \
	-of default=nw=1 "$in" || true

# The verified subset, stated as flags rather than left to defaults so a future
# x265 default change cannot silently move us outside it:
#   no-amp        AMP is out of subset (a decode mismatch, not a rejection)
#   deblock 0:0   non-zero deblock offsets are out of subset
#   wpp           wavefront IS verified (and is x265's default)
#   no-open-gop   every GOP starts at an IDR, so playback can begin anywhere
#   log-level     keep the encoder quiet; we care about the bitstream
x265_params="wpp=1:no-amp=1:deblock=0,0:no-open-gop=1:log-level=none"

# Written out longhand rather than with `$( [ ] && echo )`: under `set -e` a
# command substitution that exits non-zero makes the whole ASSIGNMENT fail, so
# the 8-bit case killed the script with no message at all.
if [ "$depth" = 10 ]; then
	vf="format=yuv420p10le"
	profile=main10
else
	vf="format=yuv420p"
	profile=main
fi
if [ "$height" != 0 ]; then vf="scale=-2:${height}:flags=lanczos,$vf"; fi
if [ "$fps" != 0 ]; then vf="fps=${fps},$vf"; fi

set -x
ffmpeg -y -hide_banner -loglevel warning \
	${secs:+-t "$secs"} -i "$in" \
	-an -sn -dn -map 0:v:0 \
	-vf "$vf" \
	-c:v libx265 -profile:v "$profile" -preset medium -crf 22 \
	-x265-params "$x265_params" \
	-color_primaries bt709 -color_trc bt709 -colorspace bt709 -color_range tv \
	-f hevc "$out"
set +x

bytes="$(stat -c%s "$out")"
echo "=== encoded: $out ($bytes bytes) ==="
ffprobe -v error -select_streams v:0 \
	-show_entries stream=width,height,pix_fmt,nb_read_packets -count_packets \
	-of default=nw=1 "$out" || true

if [ "$stage" = 1 ]; then
	name="$(basename "$out")"
	# The live root is whichever export carries fsid=0, NOT the historical
	# /srv/phoenix-rpi4-nfs (see docs + the same idiom in sync-netboot-tree.sh).
	live="$(awk '$0 ~ /fsid=0/ && $1 ~ /^\//{print $1; exit}' \
		/etc/exports /etc/exports.d/*.exports 2>/dev/null || true)"
	for root in "$repo/.buildroot/_fs/aarch64a72-generic-rpi4b/root" "$live"; do
		[ -n "$root" ] && [ -d "$root" ] || continue
		mkdir -p "$root/usr/share/demo"
		cp "$out" "$root/usr/share/demo/$name"
		echo "staged -> $root/usr/share/demo/$name"
	done
	cat <<EOF

Play it on the Pi:
  hevc-play /usr/share/demo/$name                      # full screen
  hevc-play --window 960x540+480+270 /usr/share/demo/$name   # window over the terminal
EOF
fi
