#!/usr/bin/env bash
#
# flipstat-summary.sh — turn the v3d-winsys flip counter's UART lines into a
# per-app frame-rate summary.
#
# The winsys counts frames actually scanned out and prints, every ~5 s,
#   v3d-winsys: flipstat <N> frames in <T> ms = <X.XX> fps (total <M>)
# for every GL app. This reduces those lines to count / mean / min / max.
#
# WHY A DISTRIBUTION AND NOT A NUMBER: the rate genuinely swings with the scene.
# QuakeSpasm's own demo measured 25.30-42.67 fps inside a single run (2026-09-10),
# a 1.7x spread -- which is why two honest single readings of the same app
# disagreed (31 vs 48 FPS) for days and why no A/B comparison built on one
# screenshot could ever settle anything. Quote the mean AND the range, over a
# window long enough to cover the workload.
#
# ⚠ READ A FINISHED LOG. The mean moves while a cycle is still capturing: the same
# STK log read 2.59 fps over 11 samples and 3.51 over 13 a few minutes later,
# because more lines had landed. Same trap stk-fps-from-hdmi.py documents for its
# own frame pick. Within-run and relative comparisons are fine either way; do not
# quote a mean as "app X runs at Y fps" from a log that is still growing.
#
# ⚠ NO LINES IS NOT ZERO FPS. Two cases produce silence by design:
#   * single-buffer (blit-resolve) scanout -- there are no page flips to count.
#     Check the "scanout init ... N buffer(s)" line for the buffer count first.
#   * the glamor X server, which presents by GPU readback into /dev/fb0 and never
#     reaches the winsys flip path. Its rate has to come from its own
#     `gl-x11: frame ... fps` line instead.
#   vkQuake is a third case: it presents through its own shim, so --gc-sections
#   prunes the counter out of that binary entirely.
#
# Usage: ./scripts/flipstat-summary.sh <label> [<label> ...]
#        ./scripts/flipstat-summary.sh --all-of <gate-label>   # every app of a gate run
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause
set -uo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
log_dir="${repo_root}/artifacts/rpi4b-uart"

if [ "$#" -eq 0 ]; then
	echo "usage: $0 <label> [...]   |   $0 --all-of <gate-label>" >&2
	exit 2
fi

labels=()
if [ "${1}" = "--all-of" ]; then
	gate="${2:?--all-of needs a gate label}"
	for f in "${log_dir}"/rpi4b-uart-*-"${gate}"-*.log; do
		[ -f "$f" ] || continue
		b="$(basename "$f")"; b="${b%.log}"
		labels+=("${b#rpi4b-uart-*-}")
	done
	# de-dup, keep order
	mapfile -t labels < <(printf '%s\n' "${labels[@]}" | awk '!seen[$0]++')
else
	labels=("$@")
fi

printf '%-16s %7s %8s %8s %8s   %s\n' app samples mean min max log
for lbl in "${labels[@]}"; do
	f="$(ls -t "${log_dir}"/rpi4b-uart-*"${lbl}".log 2>/dev/null | head -1)"
	if [ -z "${f}" ]; then
		printf '%-16s %7s\n' "${lbl}" "NO LOG"
		continue
	fi
	# `= <X.XX> fps` is the field; grep -a because the logs carry binary bytes.
	stats=$(grep -ao 'flipstat [0-9]* frames in [0-9]* ms = [0-9.]* fps' "${f}" \
		| sed -e 's/.*= //' -e 's/ fps//' \
		| awk 'NR==1{mn=$1;mx=$1} {s+=$1; if($1<mn)mn=$1; if($1>mx)mx=$1; n++}
		       END{ if(n) printf "%d %.2f %.2f %.2f", n, s/n, mn, mx; else printf "0 - - -" }')
	# shellcheck disable=SC2086
	set -- ${stats}
	if [ "${1}" = "0" ]; then
		printf '%-16s %7s %8s %8s %8s   %s\n' "${lbl}" 0 "(none)" - - "$(basename "${f}")"
	else
		printf '%-16s %7s %8s %8s %8s   %s\n' "${lbl}" "$1" "$2" "$3" "$4" "$(basename "${f}")"
	fi
done
echo
echo "ⓘ no samples => single-buffer scanout, the glamor X path, or vkQuake's own"
echo "  present shim — not 0 fps. See the header of this script."
