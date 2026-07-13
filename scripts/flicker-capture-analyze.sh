#!/usr/bin/env bash
#
# flicker-capture-analyze.sh — objective GLQuake flicker test.
#
# The in-engine scr_capture harness is broken by the render-to-scanout path
# (readpx reads an empty buffer). Instead, grab the REAL HDMI output via the USB
# grabber (/dev/video4) as video while Quake plays its demos, then detect
# single-frame "blink" flicker host-side (flicker-analyze.py).
#
# Modes:
#   (default) SD boot: power-cycle with the card in the Pi; the sd variant
#       auto-launches rpi4-quake -> demo1. NOTE: SD boot currently has an
#       unrelated pl011-tty/kbd bridge spam loop that can wedge quake — prefer
#       --netboot for a clean repro.
#   --netboot: card OUT of the Pi. Serve the nfsroot image, let a psh cycle
#       launch quake (clean, no kbd spam), record HDMI during the demo.
#
# Usage:
#   flicker-capture-analyze.sh [--netboot] [label] [boot_wait_secs] [rec_secs]
# Env: RPI4B_HDMI_GRABBER (default /dev/video4)
#
# Output: artifacts/flicker/<ts>-<label>/{capture.mkv,frames/,blink_*.png}
#
# Copyright 2026 Phoenix Systems
# Author: Witold Bołt
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." && pwd)"
netboot=0
if [ "${1:-}" = "--netboot" ]; then netboot=1; shift; fi
label="${1:-flicker}"
boot_wait="${2:-48}"
rec_secs="${3:-28}"
grabber="${RPI4B_HDMI_GRABBER:-/dev/video4}"

ts="$(date +%Y%m%d-%H%M%S)"
outdir="${ROOT}/artifacts/flicker/${ts}-${label}"
mkdir -p "${outdir}/frames"
vid="${outdir}/capture.mkv"
[ -e "$grabber" ] || { echo "HDMI grabber $grabber absent"; exit 1; }

record_hdmi() {
	echo "== recording ${rec_secs}s of HDMI ($grabber) -> $vid =="
	# The USB grabber cannot sustain 1080p raw (yuyv) over USB — ffmpeg gets one
	# frame then stalls and duplicates it (840 identical frames). Use the grabber's
	# MJPEG mode (compressed, sustainable at 1080p30) and copy it through so every
	# captured frame is a real, distinct grab.
	# 720p MJPEG: low enough USB bandwidth to sustain a real 30fps stream (1080p
	# over-saturates USB and the grabber delivers one frame then stalls). 720p is
	# ample for entity-blink detection. RPI4B_FLICKER_RES overrides.
	local res="${RPI4B_FLICKER_RES:-1280x720}"
	timeout $((rec_secs + 8)) ffmpeg -y -loglevel error \
		-f v4l2 -input_format mjpeg -video_size "$res" -thread_queue_size 512 -i "$grabber" \
		-t "$rec_secs" -c:v copy "$vid" || echo "(ffmpeg returned nonzero)"
	# Restore the grabber's full-HD default: -video_size above persistently changes
	# the v4l2 device format, which would otherwise leave the live-preview
	# (live-test-rpi4b.sh) opening tiny/upscaled. Reset to 1080p MJPEG.
	v4l2-ctl -d "$grabber" --set-fmt-video=width=1920,height=1080,pixelformat=MJPG >/dev/null 2>&1 || true
}

if [ "$netboot" = 1 ]; then
	# Netboot flow: launch a psh cycle (quake) in the background with HDMI
	# snapshots DISABLED (so we own /dev/video4), wait for quake's main() on the
	# UART, then record. The cycle powers the Pi off on exit.
	"${ROOT}/scripts/netboot-server-up.sh" >/dev/null 2>&1 || true
	sudo exportfs -ra >/dev/null 2>&1 || true
	sudo systemctl restart nfs-server >/dev/null 2>&1 || true; sleep 2
	uartlog_glob="${ROOT}/artifacts/rpi4b-uart/rpi4b-uart-*-${label}.log"
	rm -f $uartlog_glob 2>/dev/null || true
	echo "== launching netboot quake cycle (HDMI snapshots off) =="
	RPI4B_HDMI_GRABBER=/nonexistent setsid bash -c \
		"exec '${ROOT}/scripts/test-cycle-psh-interact.sh' --label '${label}' --wait-secs 220 --idle-secs 90 --max-cmd-secs 120 --inter-cmd-secs 8 -- '/usr/bin/rpi4-quake'" \
		>/dev/null 2>&1 < /dev/null &
	disown
	# Wait for quake main() on the UART (up to ~4 min of boot+spawn).
	echo "== waiting for quake main() on UART =="
	tries=0
	uartlog=""
	while [ $tries -lt 150 ]; do
		uartlog=$(ls -t $uartlog_glob 2>/dev/null | head -1)
		if [ -n "$uartlog" ] && grep -qa 'main() entered' "$uartlog" 2>/dev/null; then
			echo "quake started (main() entered)"; break
		fi
		sleep 2; tries=$((tries+1))
	done
	[ $tries -ge 150 ] && echo "WARN: quake main() not seen; recording anyway"
	sleep 2  # let a few demo frames render
	record_hdmi
	# let the cycle finish + power off on its own
	echo "== waiting for cycle to finish/power-off =="
	sleep 30
else
	echo "== power-cycle the Pi (SD boot -> quake autostarts) =="
	"${ROOT}/scripts/pi_power_off.sh" || true; sleep 3
	"${ROOT}/scripts/pi_power_on.sh" || { echo "power-on failed"; exit 1; }
	echo "== waiting ${boot_wait}s for boot + quake demo start =="
	sleep "${boot_wait}"
	record_hdmi
	"${ROOT}/scripts/pi_power_off.sh" || true
fi

[ -s "$vid" ] || { echo "no video captured ($vid empty)"; exit 1; }
echo "== extracting frames =="
ffmpeg -y -loglevel error -i "$vid" "${outdir}/frames/f_%04d.png"
nf=$(ls "${outdir}/frames" 2>/dev/null | wc -l)
echo "frames: $nf"
[ "$nf" -ge 3 ] || { echo "too few frames"; exit 1; }
echo "== analyzing for flicker =="
python3 "${ROOT}/scripts/flicker-analyze.py" "${outdir}/frames" "${outdir}"
echo "== artifacts in $outdir =="
