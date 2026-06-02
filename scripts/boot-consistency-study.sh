#!/usr/bin/env bash
#
# boot-consistency-study.sh — run N identical netboot cycles on the UNCHANGED
# current image and capture timestamped UART + HDMI for each, so the boots can
# be compared for determinism (logged-line content, per-phase timing, HDMI).
#
# This DELIBERATELY does NOT rebuild between boots: the whole point is to vary
# nothing but the run, and observe what the system does differently across
# otherwise-identical boots. Build once beforehand if needed, then run this.
#
# Each boot is a single ./scripts/test-cycle-netboot.sh invocation (which powers
# the Pi on, boots over TFTP, captures UART for --capture-secs, snapshots HDMI
# every RPI4B_HDMI_INTERVAL seconds, then powers the Pi off). Boots are
# sequential — there is one Pi, one UART, one HDMI grabber.
#
# Usage:
#   ./scripts/boot-consistency-study.sh <count> [start_index]
#
# Args:
#   count        number of boots to run this invocation
#   start_index  index of the first boot (default 1); labels are boot<NN>
#
# Env:
#   STUDY_CAPTURE_SECS   per-boot UART capture seconds (default 180)
#   STUDY_LABEL_PREFIX   label prefix (default "boot")
#
# Because a single (background) Bash call is capped at ~10 min wall-clock and a
# boot is ~4 min, run this in batches of <=2 boots per call, advancing
# start_index (e.g. `... 2 1`, then `... 2 3`, ... up to the desired total).
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo"

count="${1:?usage: boot-consistency-study.sh <count> [start_index] [label_prefix]}"
start="${2:-1}"
prefix="${3:-${STUDY_LABEL_PREFIX:-boot}}"
cap="${STUDY_CAPTURE_SECS:-180}"

echo "=== boot-consistency-study: $count boot(s) from index $start, cap=${cap}s ==="
echo "=== image is NOT rebuilt; capturing timestamped UART + HDMI per boot ==="

for ((i = 0; i < count; i++)); do
	idx=$((start + i))
	label="$(printf '%s%02d' "$prefix" "$idx")"
	echo ""
	echo "=== [$((i + 1))/$count] boot $label (idx $idx) starting ==="
	./scripts/test-cycle-netboot.sh --timestamp --capture-secs "$cap" --label "$label" || {
		rc=$?
		echo "=== boot $label exited rc=$rc (143 = watchdog/power-off, usually benign) ==="
	}
	echo "=== boot $label done ==="
done

echo ""
echo "=== study batch complete: $count boot(s) from index $start ==="
