#!/bin/bash
# Per-trial C1 table across a set of UART logs sharing a label prefix.
#
# Exists because grading a C1 bench by hand is easy to get wrong in the two
# ways this project has already been burnt by:
#   - a 0-fire run that never ran the workload grades identically to a clean
#     one, so FRAMES is printed next to FIRES and a trial with 0 frames is
#     marked VOID rather than counted;
#   - "armed" is the C1_HEAP_TRACE heap-creation counter, which is OFF by
#     default since 2026-09-25, so 0 there is expected and is NOT evidence the
#     detector was absent. The detectors (the chunk poisons) are always on.
#
# Usage: scripts/c1-bench-table.sh <label-prefix>      e.g. c1base
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
art="$repo/artifacts/rpi4b-uart"
pref="${1:-}"
if [ -z "$pref" ]; then
	echo "usage: $(basename "$0") <label-prefix>" >&2
	exit 2
fi

shopt -s nullglob
logs=("$art"/*"$pref"*.log)
if [ ${#logs[@]} -eq 0 ]; then
	echo "no logs matching '*${pref}*.log' under $art" >&2
	exit 1
fi

printf '%-46s %6s %6s %8s %7s %s\n' LOG FIRES ARMED FRAMES FAULTS VERDICT
tot_f=0; tot_fire=0; valid=0; void=0
for log in $(printf '%s\n' "${logs[@]}" | sort); do
	fires=$(grep -ac 'why   =' "$log")
	armed=$(grep -ac 'C1-hunt: created' "$log")
	frames=$(grep -ao 'total [0-9]*)' "$log" | tail -1 | tr -dc 0-9)
	frames=${frames:-0}
	faults=$(grep -acE 'Exception|Data Abort|Fatal' "$log")
	if [ "$frames" -eq 0 ]; then
		verdict=VOID; void=$((void+1))
	else
		verdict=ok; valid=$((valid+1)); tot_f=$((tot_f+frames)); tot_fire=$((tot_fire+fires))
	fi
	printf '%-46s %6s %6s %8s %7s %s\n' "$(basename "$log" .log | cut -c11-)" \
		"$fires" "$armed" "$frames" "$faults" "$verdict"
done

echo "---"
printf 'valid trials: %s   void (0 frames, NOT counted): %s\n' "$valid" "$void"
printf 'fires across valid trials: %s   frames: %s\n' "$tot_fire" "$tot_f"
