#!/usr/bin/env bash
#
# record-showcase-clip.sh — capture ONE showcase clip as continuous HDMI video.
#
# The capture card is a single-opener V4L2 device, so the test cycle's periodic
# PNG snapshots and this recording cannot both hold it. This drives both halves
# with the snapshots disabled (RPI4B_HDMI_INTERVAL=0) and the recorder started in
# parallel, which is the procedure record-hdmi.sh's header describes.
#
#   ./scripts/record-showcase-clip.sh <label> <secs> <psh command> [more commands...]
#
# The recording starts BEFORE the Pi is powered on, so the clip contains the whole
# boot. That is deliberate: one segment of the reel is the boot itself, and for the
# app segments make-demo-reel.sh cuts a window out by offset anyway.
#
# Output: artifacts/hdmi-video/<ts>-<label>.mp4
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
label="${1:?usage: record-showcase-clip.sh <label> <secs> <cmd>...}"
secs="${2:?usage: record-showcase-clip.sh <label> <secs> <cmd>...}"
shift 2
[ $# -ge 1 ] || { echo "need at least one psh command" >&2; exit 2; }

cd "$repo"

# The cycle needs a window at least as long as the recording, or it powers the Pi
# off mid-clip. idle-secs is an IDLE detector, so it must also exceed any quiet
# stretch inside the app.
#
# ⚠ Those defaults are for ONE long-running app. idle-secs is how long the cycle
# waits for quiet AFTER EACH COMMAND before sending the next, so with several
# commands the first one alone can outlast the whole recording -- a multi-command
# capture then records the boot, command 1, and nothing else, which reads exactly
# like a frozen console. (It cost me a wrongly-filed `fbcon-freeze` bug: the
# screen was static because nothing more had been SENT.) For a sequence of short
# commands, override with REC_IDLE_SECS=12 or so.
idle="${REC_IDLE_SECS:-$(( secs + 30 ))}"
cmax="${REC_MAX_CMD_SECS:-$(( secs + 60 ))}"
echo "=== pacing: idle-secs=$idle max-cmd-secs=$cmax for $# command(s) ==="
if [ $# -gt 1 ] && [ "$idle" -gt $(( secs / $# )) ]; then
	echo "!! WARNING: idle-secs $idle x $# commands exceeds the ${secs}s recording;" >&2
	echo "!! later commands will run AFTER the clip ends. Set REC_IDLE_SECS lower." >&2
fi

echo "=== recording '$label' for ${secs}s: $* ==="
"$repo/scripts/record-hdmi.sh" --label "$label" --secs "$secs" \
	> "$repo/artifacts/hdmi-video/$label.reclog" 2>&1 &
rec=$!

# REC_CYCLE_OPTS: extra test-cycle options, e.g. "--skip-server-up" so an experiment's hand-staged
# export files survive (netboot-server-up.sh re-syncs the export from the built rootfs).
# shellcheck disable=SC2086
RPI4B_HDMI_INTERVAL=0 "$repo/scripts/test-cycle-psh-interact.sh" ${REC_CYCLE_OPTS:-} \
	--label "rec-$label" --wait-secs 220 --inter-cmd-secs 8 \
	--idle-secs "$idle" --max-cmd-secs "$cmax" -- "$@" \
	> "$repo/artifacts/hdmi-video/$label.cyclog" 2>&1 &
cyc=$!

wait "$rec"; recrc=$?
wait "$cyc" 2>/dev/null || true

tail -1 "$repo/artifacts/hdmi-video/$label.reclog"
exit "$recrc"
