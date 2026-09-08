#!/usr/bin/env bash
#
# Assemble the per-app HDMI captures into ONE showcase reel.
#
# The individual clips in artifacts/hdmi-video/ are raw 5-minute captures that
# each open with several minutes of boot console before the app appears -- fine as
# evidence, useless to show anyone. This cuts the interesting window out of each,
# labels it, and concatenates them into a single file.
#
# Host-side only: reads existing captures, never touches the Pi. Segments are
# listed below as "<clip basename>|<start s>|<length s>|<label>" -- edit that
# table when new footage supersedes a clip. Offsets were chosen by sampling
# frames; re-check them if a clip is re-recorded, because they are positions in
# a specific capture, not properties of the app.
#
# Output: artifacts/hdmi-video/<ts>-phoenix-rtos-rpi4-showcase.mp4
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
vid_dir="${RPI4B_HDMI_VIDEO_DIR:-$repo/artifacts/hdmi-video}"
out="${1:-$vid_dir/$(date -u +%Y%m%d-%H%M%S)-phoenix-rtos-rpi4-showcase.mp4}"

segments=(
	"20260908-004253-demo-x-and-quake|88|22|X11 desktop — Window Maker, xterm, xclock, xcalc (glamor GPU-accelerated X on V3D)"
	"20260908-004253-demo-x-and-quake|200|20|QuakeSpasm — OpenGL on Mesa v3d"
	"20260908-020040-demo-quake2|200|20|Quake II — yQuake2, OpenGL"
	"20260908-030847-demo-quake3|174|20|Quake III Arena — OpenGL"
	"20260908-054838-demo-vkquake|214|20|vkQuake — Vulkan via V3DV"
	"20260908-103201-stk-driven-5fps|180|24|SuperTuxKart 1.4 — OpenGL ES 3.1, 4-kart race"
)

command -v ffmpeg >/dev/null 2>&1 || { echo "make-demo-reel: ffmpeg not found" >&2; exit 1; }

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
list="$tmp/list.txt"
: > "$list"

i=0
for seg in "${segments[@]}"; do
	IFS='|' read -r clip start len label <<< "$seg"
	src="$vid_dir/$clip.mp4"
	if [ ! -f "$src" ]; then
		echo "make-demo-reel: missing clip $src" >&2
		exit 1
	fi
	i=$((i + 1))
	part="$tmp/part$i.mp4"
	# Escape drawtext metacharacters in the label (: and ' are the ones that bite).
	esc="${label//:/\\:}"
	esc="${esc//\'/}"
	printf 'make-demo-reel: [%d/%d] %s +%ss %ss\n' "$i" "${#segments[@]}" "$clip" "$start" "$len"
	# Re-encode every segment with identical parameters so the concat demuxer can
	# join them without a filter graph. A banner strip keeps the label legible over
	# both the bright kart track and the very dark Quake interiors.
	# The label shows for the first 4 s of each segment and then gets out of the
	# way: a permanent bottom banner clipped real HUD (Quake III's health/armour
	# digits, Quake II's ammo strip, SuperTuxKart's speedometer all live in the
	# bottom 64 px), which is exactly the detail a showcase is meant to show.
	ffmpeg -y -hide_banner -loglevel error \
		-ss "$start" -t "$len" -i "$src" \
		-vf "drawbox=x=0:y=ih-64:w=iw:h=64:color=black@0.62:t=fill:enable='lt(t,4)',\
drawtext=text='$esc':x=24:y=h-44:fontsize=26:fontcolor=white:enable='lt(t,4)'" \
		-c:v libx264 -preset veryfast -crf 20 -pix_fmt yuv420p -r 30 -an \
		"$part" </dev/null
	printf "file '%s'\n" "$part" >> "$list"
done

ffmpeg -y -hide_banner -loglevel error -f concat -safe 0 -i "$list" \
	-c copy -movflags +faststart "$out" </dev/null

bytes="$(stat -c%s "$out")"
dur="$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$out" 2>/dev/null || echo '?')"
printf 'make-demo-reel: OK  %s  (%s bytes, %s s, %d segments)\n' \
	"$out" "$bytes" "$dur" "${#segments[@]}"
