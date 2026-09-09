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
# table when new footage supersedes a clip.
#
# Every segment but the desktop now has motion. QuakeSpasm/vkQuake play id1
# demo1/demo2, Quake II plays q2demo1, STK is an AI-driven race, and Quake III is
# a live bot deathmatch.
#
# Quake III took a detour worth recording. Its shipped demos are the 1999 `.dm3`
# protocol while the engine only ever looks for `demos/<name>.dm_66/67/68/71`, so
# they can never be found and that protocol is unsupported -- demo playback is a
# netcode project, not a config. Bots work instead (the demo pak ships botfiles/
# and q3dm1.aas). The window chosen is 15 s because our own player is stationary
# and gets fragged, and the scoreboard overlay then covers the screen; 151-166 is
# a clean stretch with a bot running through frame. Trying to get a MOVING camera
# via `+team spectator`/`+follow` did not take (the HUD still showed our health),
# and `+set cg_thirdPerson 1 +set cg_cameraOrbit 2` broke startup outright -- the
# game never left the main menu, because those are cgame cvars that cannot be set
# before the game module loads. Offsets were chosen by sampling
# frames; re-check them if a clip is re-recorded, because they are positions in
# a specific capture, not properties of the app.
#
# Output: artifacts/hdmi-video/<ts>-phoenix-rtos-rpi4-showcase.mp4
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
vid_dir="${RPI4B_HDMI_VIDEO_DIR:-$repo/artifacts/hdmi-video}"
out="${1:-$vid_dir/$(date -u +%Y%m%d-%H%M%S)-phoenix-rtos-rpi4-showcase.mp4}"

# The showcase, in the order the owner asked for (2026-09-08): boot once at the
# start, then shell action, then X11 with movement and the GL-accelerated
# window, then the browser and video, then the games.
#
# Every clip below is a real HDMI capture of the Pi running the netboot root --
# nothing is a host recording or a screen-grab of a desktop emulator. The
# windows were chosen by measuring the clips, not by eye: for the games, the
# usable window is bounded because `+playdemo demo1` plays ONE demo and then
# drops to the console; and the first ~30 s of any capture is skipped because the
# grabber's first frames are a stale pink card, not the Pi.
#
# The X11 clip was re-recorded 2026-09-09 and replaces the old x-restored capture,
# which predated the whole X11 fix series and showed the mirrored-Clip artefact,
# grey bands bleeding between xterms, xbill hidden under another window, and a
# desktop reaching HDMI only ~2.3 times a second. The replacement was measured
# rather than eyeballed: across all 100 s of it the desktop is fully populated
# (22-24% bright pixels) and xbill's board is 95.5% white, i.e. every client is up
# and unobstructed for the entire clip, so any window works -- the old note about
# stopping at ~150 s "because life.py freezes" no longer applies (that was Conway
# converging to still lifes, not a hang).
#
# The four Quake segments and SuperTuxKart all carry the engine's OWN on-screen
# frame-rate readout, so the performance figures in this reel are the system
# reporting itself rather than a claim in a caption.
segments=(
	"20260908-202248-shell-demo|32|24|Boot — plo -> kernel -> lwIP -> NFS root -> psh, on real hardware"
	"20260908-202248-shell-demo|112|26|Shell — uname, the ported /usr/bin userland, Lua 5.4.7 / jq 1.7.1 / Python 3.14.4"
	"20260908-202248-shell-demo|164|22|Python 3.14 + ncurses — Conway's Game of Life, 239x66 on the HDMI console"
	"20260909-122231-x-shipped-image|20|26|X11 desktop — Window Maker on glamor GPU-accelerated X: live OpenGL window, Python 3.14 + ncurses Game of Life, top, xbill and xclock"
	"20260908-161800-dillo-browse|38|13|Dillo web browser — page fetched over TCP/IP, rendered under glamor X"
	"20260909-161007-owner-video-hw|5|24|Hardware H.265 decode — BCM2711 rpivid decoding a 1080p phone recording, full-screen at 21.7 fps"
	"20260908-191446-qs-fps2|95|22|QuakeSpasm — OpenGL on Mesa v3d, id1 demo1 playback, 35 FPS on screen"
	"20260908-193011-q2-fps|112|20|Quake II — yQuake2 on OpenGL ES, q2demo1 playback, 27.8 fps on screen"
	"20260908-192454-vkq-fps|158|22|vkQuake — Vulkan via V3DV, id1 demo2 playback, 73 FPS on screen"
	"20260908-211352-q3-smooth2|210|24|Quake III Arena — 5-bot deathmatch on q3dm1, orbiting camera, 46 fps on screen"
	"20260908-182836-stk-fps3|196|24|SuperTuxKart 1.4 — OpenGL ES 3.1, 4-kart AI race"
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
	# join them without a filter graph.
	#
	# Colour: the capture card delivers FULL-range MJPEG, which ffmpeg carried
	# through as yuvj420p tagged color_range=pc with color_space=bt470bg -- an SD
	# matrix on HD content, primaries and transfer unknown. Self-consistent, so
	# compliant players got it right, but yuvj420p is deprecated and that tagging
	# is not the portable form for a video that may be published. Convert to
	# limited range and tag bt709 explicitly. A/B'd on a Quake III frame before
	# adopting it: visually identical, only the expected range round-trip. A banner strip keeps the label legible over
	# both the bright kart track and the very dark Quake interiors.
	# The label shows for the first 4 s of each segment and then gets out of the
	# way: a permanent bottom banner clipped real HUD (Quake III's health/armour
	# digits, Quake II's ammo strip, SuperTuxKart's speedometer all live in the
	# bottom 64 px), which is exactly the detail a showcase is meant to show.
	ffmpeg -y -hide_banner -loglevel error \
		-ss "$start" -t "$len" -i "$src" \
		-vf "drawbox=x=0:y=ih-64:w=iw:h=64:color=black@0.62:t=fill:enable='lt(t,4)',\
drawtext=text='$esc':x=24:y=h-44:fontsize=26:fontcolor=white:enable='lt(t,4)',\
scale=in_range=pc:out_range=tv,format=yuv420p" \
		-c:v libx264 -preset veryfast -crf 20 -r 30 -an \
		-color_range tv -colorspace bt709 -color_primaries bt709 -color_trc bt709 \
		"$part" </dev/null
	printf "file '%s'\n" "$part" >> "$list"
done

ffmpeg -y -hide_banner -loglevel error -f concat -safe 0 -i "$list" \
	-c copy -movflags +faststart "$out" </dev/null

bytes="$(stat -c%s "$out")"
dur="$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$out" 2>/dev/null || echo '?')"
printf 'make-demo-reel: OK  %s  (%s bytes, %s s, %d segments)\n' \
	"$out" "$bytes" "$dur" "${#segments[@]}"
