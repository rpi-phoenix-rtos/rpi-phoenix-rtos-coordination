#!/usr/bin/env bash
#
# Phoenix-RTOS RPi4 — record the Pi's HDMI output to a shareable MP4.
#
# The test cycles already grab periodic PNG snapshots (artifacts/hdmi/*-tick.png),
# which are right for automated grading but useless as a demo. This records
# continuous video instead, for the owner's "screen recording published online".
#
# ⚠️  The capture card is a single-opener V4L2 device: this and the test cycle's
# periodic snapshots CANNOT both hold it. Run a cycle with snapshots disabled:
#
#     RPI4B_HDMI_INTERVAL=0 ./scripts/test-cycle-psh-interact.sh --label demo \
#         --wait-secs 150 --idle-secs 240 --max-cmd-secs 300 -- "startx_gpu deskapps"
#
#   and start this script in parallel (it powers nothing itself — it only reads
#   the grabber, so it is safe to run next to a cycle and does not touch the UART
#   or the Pi lock).
#
# Host-side only. Does NOT boot the Pi and does NOT touch the flagship image.

set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

grabber="${RPI4B_HDMI_GRABBER:-/dev/video4}"
out_dir="${RPI4B_HDMI_VIDEO_DIR:-$repo/artifacts/hdmi-video}"
label=""
secs=120
size="1920x1080"
fps=30
fmt="mjpeg"
crf=18
out=""

usage() {
	cat <<EOF
Usage: $(basename "$0") [options]

  --label <name>    name fragment for the output file (default: none)
  --secs <N>        recording length in seconds (default: $secs)
  --size <WxH>      capture size (default: $size)
  --fps <N>         capture framerate (default: $fps)
  --format <f>      v4l2 input format: mjpeg or yuyv422 (default: $fmt)
  --crf <N>         x264 quality, lower is better (default: $crf)
  --out <path>      explicit output path (overrides --label/dir)
  -h, --help        this text

Output: \$RPI4B_HDMI_VIDEO_DIR/<ts>-<label>.mp4 (default $out_dir)

mjpeg is the default input format because the card delivers 1080p30 MJPEG within
USB bandwidth; yuyv422 is uncompressed and may drop frames at 1080p.
EOF
}

while [ $# -gt 0 ]; do
	case "$1" in
	--label) label="${2:?--label needs a value}"; shift 2 ;;
	--secs) secs="${2:?--secs needs a value}"; shift 2 ;;
	--size) size="${2:?--size needs a value}"; shift 2 ;;
	--fps) fps="${2:?--fps needs a value}"; shift 2 ;;
	--format) fmt="${2:?--format needs a value}"; shift 2 ;;
	--crf) crf="${2:?--crf needs a value}"; shift 2 ;;
	--out) out="${2:?--out needs a value}"; shift 2 ;;
	-h | --help) usage; exit 0 ;;
	*) printf 'unknown argument: %s\n\n' "$1" >&2; usage >&2; exit 2 ;;
	esac
done

command -v ffmpeg >/dev/null 2>&1 || { echo "record-hdmi: ffmpeg not found" >&2; exit 1; }

if [ ! -e "$grabber" ]; then
	echo "record-hdmi: grabber $grabber absent — is the capture card plugged in?" >&2
	exit 1
fi

# A cycle's periodic snapshotter holds the device; fail early with the fix rather
# than letting ffmpeg emit a bare EBUSY.
if ! ffmpeg -hide_banner -loglevel error -f v4l2 -i "$grabber" -frames:v 1 -f null - \
	</dev/null >/dev/null 2>&1; then
	cat >&2 <<EOF
record-hdmi: cannot open $grabber for capture.

The capture card allows a single opener. If a test cycle is running with its
periodic PNG snapshots enabled, it owns the device — re-run that cycle with
RPI4B_HDMI_INTERVAL=0 and start this script alongside it.
EOF
	exit 1
fi

mkdir -p "$out_dir"
if [ -z "$out" ]; then
	ts="$(date -u +%Y%m%d-%H%M%S)"
	if [ -n "$label" ]; then out="$out_dir/$ts-$label.mp4"; else out="$out_dir/$ts.mp4"; fi
fi

printf 'record-hdmi: %s -> %s\n' "$grabber" "$out"
printf 'record-hdmi: %s @ %s fps, %s in, %s s, x264 crf %s\n' "$size" "$fps" "$fmt" "$secs" "$crf"

# -yuv420p so the file plays in browsers and every player; -movflags +faststart
# so it streams without a full download. Not -c copy: raw MJPEG in MP4 is huge
# and many players refuse it.
set +e
ffmpeg -y -hide_banner -loglevel warning \
	-f v4l2 -input_format "$fmt" -video_size "$size" -framerate "$fps" \
	-i "$grabber" \
	-t "$secs" \
	-c:v libx264 -preset veryfast -crf "$crf" -pix_fmt yuv420p \
	-movflags +faststart \
	"$out" </dev/null
rc=$?
set -e

if [ "$rc" -ne 0 ] || [ ! -s "$out" ]; then
	echo "record-hdmi: FAILED (ffmpeg rc=$rc)" >&2
	exit 1
fi

bytes="$(stat -c%s "$out")"
dur="$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$out" 2>/dev/null || echo '?')"
printf 'record-hdmi: OK  %s  (%s bytes, %s s)\n' "$out" "$bytes" "$dur"
