#!/usr/bin/env bash
#
# Assemble the browser showcase's HDMI recordings into ONE reel (docs/BROWSER-SHOWCASE-PLAN.md).
#
# The browser counterpart of make-demo-reel.sh, with the same cut, caption and encoding: every
# segment is cut out of a recording at REAL-TIME SPEED. Nothing is sped up: a page that takes 14 s
# to load takes 14 s on the reel too (the capture is 30 fps and the reel is 30 fps, no setpts).
#
#   scripts/make-browser-reel.sh [--list] [output.mp4]
#
#   --list   resolve the cut list (clips, anchors, offsets) and print it; encode nothing
#
# Segments are "<clip>|<start>|<length s>|<label>", as in make-demo-reel.sh, with two additions:
#
#   <clip>   a recording's basename (20261009-101500-bshow-sites), or just its LABEL
#            (bshow-sites): the newest artifacts/hdmi-video/*-<label>.mp4. Pin the basename once
#            the reel is final, so a re-record does not silently change it.
#   <start>  seconds into the recording, or an ANCHOR: @<item>.<event>[+|-<seconds>], e.g.
#            @sites.load1+2 = two seconds after the first page of the WPE sites item finished
#            loading. scripts/browser-reel-events.py finds the anchors in the clip's UART log
#            (the items' "BSHOW item=… start epoch=" lines and the browsers' WPEB/WKGB t= lines).
#
# BROWSER_REEL_SEGMENTS=<file> replaces the table below with the lines of <file> (same format; a
# smoke test against existing clips). BROWSER_REEL_LAG (default 1.0) is browser-reel-events.py's
# --lag. Check the result with
#
#   scripts/verify-demo-reel.py <reel.mp4> --segments scripts/make-browser-reel.sh \
#       --static-ok 'Boot,WPE WebKit,WebKitGTK'
#
# (web pages are legitimately still between keystrokes; the GPU and video segments must move).
#
# Output: artifacts/hdmi-video/<ts>-phoenix-rtos-rpi4-browser-showcase.mp4
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
vid_dir="${RPI4B_HDMI_VIDEO_DIR:-$repo/artifacts/hdmi-video}"
list_only=0
if [ "${1:-}" = --list ]; then
	list_only=1
	shift
fi
out="${1:-$vid_dir/$(date -u +%Y%m%d-%H%M%S)-phoenix-rtos-rpi4-browser-showcase.mp4}"
lag="${BROWSER_REEL_LAG:-1.0}"

# The reel, in scene order (about 4 minutes). Clips by label until the recordings are final;
# starts are anchors, so a re-recorded clip needs no new offsets. The anchors are explained in
# scripts/browser-reel-events.py; the scene timelines in tools/browser/showcase/pi/*.sh.
#
#   sites  wpe-sites.sh: start page 0 s, Ctrl+L 6, Wikipedia typed 7-11, Return 12 (go1),
#          load1 ~26, Page_Down 32/35/38, search typed 45, Return 47 (go2, load2 ~49),
#          GitHub typed 58-63, Return 64 (go3, load3 ~75), Page_Down 82/86/90, back 96/102, quit 111
#   gpu    wpe-gpu.sh: the page draws from ~3 s
#   hls    wpe-hls.sh: load1 = the page, the first picture ~2 s later
#   demo   wpe-demo.sh: the same
#   gtk    gtk-tabs.sh: tabs load from ~3 s, the download after load1, a tab switch every 12 s
segments=(
	"bshow-sites|20|12|Boot — Raspberry Pi 4 netboot: the Phoenix-RTOS kernel, drivers, GPU servers and the NFS root"
	"bshow-sites|@sites.start+4|40|WPE WebKit 2.54 — an address typed in real time, then Wikipedia over HTTPS, scrolled"
	"bshow-sites|@sites.go2-3|12|WPE WebKit 2.54 — a DuckDuckGo search"
	"bshow-sites|@sites.go3-7|48|WPE WebKit 2.54 — GitHub, the Phoenix-RTOS kernel; then back, back"
	"bshow-gpu|@gpu.start+6|30|WebGL on the V3D GPU — a lit 19,200-triangle knot, and CSS animation composited on the GPU"
	"bshow-video|@hls.load1+3|25|HEVC 1080p on the hardware decoder, zero-copy — an HLS stream; the player chose the 1080p HEVC variant"
	"bshow-video|@demo.load1+3|25|HEVC 1080p30 in a web page on the hardware decoder, zero-copy — our own showcase reel as a video file"
	# optional (scene.sh mse): hls.js starts low and climbs; cut where the label changes
	# "bshow-mse|@mse.load1+8|25|hls.js over Media Source Extensions — adaptive bitrate climbs to HEVC 1080p on the hardware decoder"
	"bshow-gtk|@gtk.start+6|50|WebKitGTK 2.54 — tabs and downloads: Wikipedia, GitHub, Python docs, a 23 MB download"
)
if [ -n "${BROWSER_REEL_SEGMENTS:-}" ]; then
	segments=()
	while IFS= read -r line; do
		line="${line#"${line%%[![:space:]]*}"}"
		line="${line#\"}"
		line="${line%\"}"
		case "$line" in '' | '#'*) continue ;; esac
		segments+=("$line")
	done < "$BROWSER_REEL_SEGMENTS"
fi
[ "${#segments[@]}" -gt 0 ] || { echo "make-browser-reel: no segments" >&2; exit 1; }

command -v ffmpeg >/dev/null 2>&1 || { echo "make-browser-reel: ffmpeg not found" >&2; exit 1; }

# clip_path <clip>: a basename, or the newest recording with that label
clip_path() {
	local c=$1 f
	if [ -f "$vid_dir/$c.mp4" ]; then
		echo "$vid_dir/$c.mp4"
		return
	fi
	f="$(compgen -G "$vid_dir/*-$c.mp4" | LC_ALL=C sort | tail -1 || true)"
	[ -n "$f" ] || { echo "make-browser-reel: no recording $vid_dir/$c.mp4 or $vid_dir/*-$c.mp4" >&2; return 1; }
	echo "$f"
}

# resolve <clip path> <start>: seconds; anchors come from browser-reel-events.py, once per clip
declare -A anchors_of
resolve() {
	local src=$1 spec=$2 name delta a
	case "$spec" in
		@*) ;;
		*) echo "$spec"; return ;;
	esac
	if [[ ! "$spec" =~ ^@([A-Za-z0-9_-]+\.[A-Za-z]+[0-9]*)([+-][0-9]+(\.[0-9]+)?)?$ ]]; then
		echo "make-browser-reel: bad start '$spec' (want @<item>.<event>[+|-<s>])" >&2
		return 1
	fi
	name=${BASH_REMATCH[1]}
	delta=${BASH_REMATCH[2]:-+0}
	if [ -z "${anchors_of[$src]+x}" ]; then
		anchors_of[$src]="$(python3 "$repo/scripts/browser-reel-events.py" "$src" --anchors --lag "$lag")" || return 1
	fi
	a="$(printf '%s\n' "${anchors_of[$src]}" | sed -n "s/^${name//./\\.}=//p" | head -1)"
	[ -n "$a" ] || { echo "make-browser-reel: anchor $name not in $(basename "$src")'s UART log (browser-reel-events.py $src)" >&2; return 1; }
	awk -v a="$a" -v d="$delta" 'BEGIN { s = a + d; if (s < 0) s = 0; printf "%.1f\n", s }'
}

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
list="$tmp/list.txt"
: > "$list"

i=0
total=0
for seg in "${segments[@]}"; do
	IFS='|' read -r clip spec len label <<< "$seg"
	src="$(clip_path "$clip")"
	start="$(resolve "$src" "$spec")"
	dur="$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$src" 2>/dev/null || echo 0)"
	# a cut past the end of the recording runs into the capture card's no-signal field
	if awk -v s="$start" -v l="$len" -v d="$dur" 'BEGIN { exit !(s + l > d) }'; then
		echo "make-browser-reel: segment $((i + 1)) ($spec = ${start}s +${len}s) runs past the end of $(basename "$src") (${dur}s)" >&2
		exit 1
	fi
	i=$((i + 1))
	total=$((total + len))
	printf 'make-browser-reel: [%d/%d] %s %s = +%ss %ss  %s\n' "$i" "${#segments[@]}" "$(basename "$src" .mp4)" "$spec" "$start" "$len" "$label"
	[ "$list_only" = 1 ] && continue
	part="$tmp/part$i.mp4"
	# drawtext metacharacters in the label (: and ' are the ones that bite)
	esc="${label//:/\\:}"
	esc="${esc//\'/}"
	# The same cut, caption band and encoding as make-demo-reel.sh (its comments say why): fast
	# seek (the captures are all-keyframe), the caption for the first 4 s in a band 136-200 px
	# above the bottom edge, full-range MJPEG converted to limited-range BT.709, 30 fps.
	ffmpeg -y -hide_banner -loglevel error \
		-ss "$start" -t "$len" -i "$src" \
		-vf "drawbox=x=0:y=ih-200:w=iw:h=64:color=black@0.62:t=fill:enable='lt(t,4)',\
drawtext=text='$esc':x=24:y=h-181:fontsize=26:fontcolor=white:enable='lt(t,4)',\
scale=in_range=pc:out_range=tv,format=yuv420p" \
		-c:v libx264 -preset veryfast -crf 20 -r 30 -an \
		-color_range tv -colorspace bt709 \
		-x264-params "colorprim=bt709:transfer=bt709" \
		"$part" </dev/null
	printf "file '%s'\n" "$part" >> "$list"
done

if [ "$list_only" = 1 ]; then
	printf 'make-browser-reel: %d segments, %d s (nothing encoded: --list)\n' "${#segments[@]}" "$total"
	exit 0
fi

ffmpeg -y -hide_banner -loglevel error -f concat -safe 0 -i "$list" \
	-c copy -movflags +faststart "$out" </dev/null

bytes="$(stat -c%s "$out")"
dur="$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$out" 2>/dev/null || echo '?')"
printf 'make-browser-reel: OK  %s  (%s bytes, %s s, %d segments, table %d s)\n' \
	"$out" "$bytes" "$dur" "${#segments[@]}" "$total"
