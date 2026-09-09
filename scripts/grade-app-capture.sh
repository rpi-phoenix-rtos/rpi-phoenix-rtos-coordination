#!/usr/bin/env bash
#
# grade-app-capture.sh — turn one HDMI capture + its UART log into a PASS/CHECK line.
#
# Why this exists: "the frame looks fine" is not a gate. Three things have to hold
# for an app to count as working on an image, and each has been wrong at least once
# in this project while the other two were right:
#
#   1. no fault pattern in the UART log      (a crash after the first frame)
#   2. the frame is not blank or flat         (a black screen, or a flat grey one)
#   3. the picture CHANGES between two times  (a rendered-then-frozen frame)
#
# (3) is the one that catches the most: an ffmpeg blackframe check passes on a text
# console, and a single still frame cannot tell a running game from a hung one. It
# also caught a mistake of the operator's rather than the target's — grading
# SuperTuxKart as CHECK when it had been launched with no race argument and was
# sitting correctly in its own menu.
#
# Usage: grade-app-capture.sh <label> [t1] [t2]
#   <label>  the --label given to test-cycle-psh-interact.sh / record-hdmi.sh
#   t1 t2    two capture timestamps (seconds) to compare; default 95 and 105
#
# Exit status is 0 for PASS, 1 for CHECK/NO VIDEO, so it can gate a script.

set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
lbl="${1:?usage: grade-app-capture.sh <label> [t1] [t2]}"
t1="${2:-95}"
t2="${3:-105}"

faults="$("$repo/scripts/uart-summary.sh" "$lbl" 2>&1 | grep -A1 FAULTS | tail -1)"
vid="$(ls -t "$repo"/artifacts/hdmi-video/*"$lbl"*.mp4 2>/dev/null | head -1)"
if [ -z "$vid" ]; then
	printf '%-14s NO VIDEO (no artifacts/hdmi-video/*%s*.mp4)\n' "$lbl" "$lbl"
	exit 1
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
ffmpeg -v error -ss "$t1" -i "$vid" -frames:v 1 -y "$tmp/a.png" || exit 1
ffmpeg -v error -ss "$t2" -i "$vid" -frames:v 1 -y "$tmp/b.png" || exit 1

"$repo/.venv/bin/python" - "$tmp" "$lbl" "$faults" <<'PY'
import sys
import numpy as np
from PIL import Image

tmp, lbl, faults = sys.argv[1], sys.argv[2], sys.argv[3]
a = np.asarray(Image.open(tmp + "/a.png").convert("L")).astype(np.int16)
b = np.asarray(Image.open(tmp + "/b.png").convert("L")).astype(np.int16)

motion = float(np.abs(a - b).mean())
ok_frame = a.std() > 8.0          # blank or flat-colour screens sit well below this
ok_motion = motion > 1.0          # a frozen frame gives ~0
ok_faults = faults.strip().endswith("0")

why = []
if not ok_frame:
    why.append("frame blank/flat")
if not ok_motion:
    why.append("no motion")
if not ok_faults:
    why.append("faults in log")

verdict = "PASS" if not why else "CHECK"
print(f"{lbl:14s} {verdict}  mean={a.mean():6.1f} std={a.std():5.1f} "
      f"motion={motion:5.1f}  {faults.strip()}"
      + ("  <- " + ", ".join(why) if why else ""))
sys.exit(0 if not why else 1)
PY
