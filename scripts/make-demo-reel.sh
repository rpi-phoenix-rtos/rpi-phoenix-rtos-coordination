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
# The X11 clip is the 2026-09-15 `startx_gpu action` capture. Measured rather than
# eyeballed: over the chosen window the desktop never freezes (mean frame-to-frame
# 4.54, min 2.40, 0 frozen steps, 0 black frames), the GL window never stalls, and
# Life is live at 17.4 gen/s -- matching its own on-screen readout. All five
# `action`-mode clients are drawn, unobstructed and not half-painted.
#
# NOTE, checked 2026-09-15: in THIS capture xbill sits on its title screen (0.00
# motion across the whole clip) and xclock has no second hand. The older note here
# claimed "xbill's board is 95.5% white, every client up and animating" -- that
# described the 2026-09-09 clip and is not true of this one, so do not grade a
# future X capture against it.
#
# WHERE THE FRAME-RATE FIGURES COME FROM, and the trap in measuring them.
#
# Two different instruments appear below, and the captions say which:
#
#   "N fps on screen"        -- the engine's OWN readout, burned into the frame.
#                               A viewer can read it off the video and check us.
#   "N fps at the page flip" -- the winsys `flipstat` counter from the UART log,
#                               which counts frames actually PRESENTED. This is
#                               the trustworthy number; an engine's on-screen
#                               counter is a ~1 s instantaneous estimate and
#                               swings far wider (vkQuake's reads 23-65 across
#                               the 22 s shown here).
#
# NOTE: the vkQuake clip was captured 2026-09-15, BEFORE the demo-loop fix (fork c55d7d8), so that
# capture plays demo2 once and then sits on the console. The window cut here (112-134 s) is inside
# demo2 and is unaffected -- but do not read that clip as evidence of current behaviour, which loops
# demo1/demo2/demo3 indefinitely. Not worth a re-record for a caption that is already accurate.
#
# ⚠ A flipstat figure must be averaged over the GAMEPLAY windows ONLY. Taking a
# median over the whole run measures whatever the app sat on longest, which is
# usually a motionless screen. vkQuake plays one demo and then drops to the
# console, and that static console renders at a dead-flat 42.2 fps for the rest
# of the capture -- so the whole-run median IS the console, not the demo. (It
# landed within 1 fps of the real answer here purely by chance, which is exactly
# why it went unnoticed.) The figures below are frame-weighted means over the
# gameplay windows: vkQuake 2140 frames / 50.1 s = 42.7, STK 2248 / 282.9 = 7.95.
segments=(
	"20260930-042356-shell-net|15|15|Boot — Raspberry Pi 4 netboot: Phoenix-RTOS kernel, drivers, the GPU servers, NFS root"
	"20260930-042356-shell-net|105|30|Shell and networking — Ethernet and WiFi (joined at boot from /etc/wifi.conf), ping, Python 3.14"
	"20260930-042717-xfce-apps|170|14|Atril PDF viewer on the XFCE 4.20 desktop (Wayland) — GTK 3, next to Thunar and a terminal"
	"20260930-042717-xfce-apps|228|25|Quake III Arena in a window — GPU-accelerated on Wayland, beside Thunar and foot, ~74 fps"
	"20260930-042717-xfce-apps|288|30|Video player — H.264 720p in a window, then full screen, 30 fps"
	"20260930-092433-g5-verify|295|25|SuperTuxKart 1.4 in a window — a 4-kart race on the XFCE desktop, ~21 fps"
	"20260930-044818-x11|180|26|X11 — Xorg + glamor, Window Maker: a 60 fps EGL window, Python Game of Life, top on 4 CPUs, xbill, xclock"
	"20260930-045535-dillo|180|13|Dillo — a live web page over HTTPS"
	"20260930-050107-fs-q3|158|18|Quake III Arena full screen — bot deathmatch on q3dm1, ~59 fps"
	"20260930-050547-fs-q2|118|22|Quake II full screen — demo playback, 60 fps"
	"20260930-051028-fs-qs|130|22|QuakeSpasm full screen — the id1 demo, ~44 fps"
	"20260930-053629-fs-vkq3|402|22|vkQuake full screen — Vulkan on V3DV, demo1 playback, ~42-55 fps"
	"20260930-051949-fs-stk|180|25|SuperTuxKart full screen — AI race rendered at 1280x720, scaled to 1080p by the display"
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
	#
	# It sits 136-200 px ABOVE the bottom edge, not flush with it. Flush is where
	# every video player draws its scrub bar and controls, so the caption -- the
	# part that says what you are looking at -- was routinely hidden behind them
	# (owner, 2026-09-16). This band clears typical controls with margin while
	# still staying out of the games' bottom-edge HUDs.
	# NOTE on `-ss` BEFORE `-i`: that is ffmpeg's FAST (keyframe) seek, which in
	# general lands on the nearest preceding keyframe rather than the requested
	# time -- and every offset in the table above was derived with ACCURATE seeks.
	# Verified 2026-09-15 for all 11 segments: fast and accurate seek return the
	# SAME frame, MAD 0.000, because record-hdmi.sh's captures are effectively
	# all-keyframe. Keep it, but if the capture encoder ever grows a longer GOP,
	# every cut silently shifts -- re-check with that comparison before trusting
	# the offsets again.
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

# Colour tags ride in from the segment bitstreams; `-c copy` preserves them, and
# re-stating them here was measured to change nothing.
ffmpeg -y -hide_banner -loglevel error -f concat -safe 0 -i "$list" \
	-c copy -movflags +faststart "$out" </dev/null

bytes="$(stat -c%s "$out")"
dur="$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$out" 2>/dev/null || echo '?')"
printf 'make-demo-reel: OK  %s  (%s bytes, %s s, %d segments)\n' \
	"$out" "$bytes" "$dur" "${#segments[@]}"
