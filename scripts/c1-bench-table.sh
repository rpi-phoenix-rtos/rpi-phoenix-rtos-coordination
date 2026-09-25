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

printf '%-38s %5s %6s %6s %8s %7s %5s %s\n' LOG SIG GUARD ARMED FRAMES FAULTS TDOWN VERDICT
tot_f=0; tot_fire=0; valid=0; void=0
for log in $(printf '%s\n' "${logs[@]}" | sort); do
	# ⚠ Count EVERY allocator guard, not just the corrupt-header one. The
	# `why   =` line belongs to one guard; the page-poison guard prints
	# "POISON BROKEN" and no `why`. Counting only `why` called
	# c1armA-3 clean when it had 4 poison breaks, and produced a bogus
	# "C1 has gone quiet" reading across the whole archive on 2026-09-25.
	# Two independent columns, because neither alone is trustworthy:
	#
	#  SIG   the C1 signature itself -- 0x80000000/0x80000001 sitting in the HIGH
	#        half of a 64-bit word, however it is reported (hhi32, p4got, a
	#        corrupted hsize/heap). This is the selector to believe: it does not
	#        care which guard printed it.
	#  GUARD any allocator guard message at all. Kept because a guard can fire
	#        WITHOUT the signature (that is a different defect, e.g. c1keep-4's
	#        "small-bin head is not a chunk"), and because it catches new guards.
	#
	# Guard-NAME counting was tried and is fragile: malloc_dl.c can emit 20+
	# distinct literals, and picking a subset produced two wrong conclusions on
	# 2026-09-25 -- first `why   =` alone (which misses the page-poison guard
	# entirely), then a five-message list that still omitted "free-bin link is not
	# a plausible chunk", which has fired in 17 archived logs.
	fires=$(grep -acE 'hhi32 = 0x0*8000000[01]|p4got  = 0x0*8000000[01]|= 0x8000000[01][0-9a-f]{8}' "$log")
	guards=$(grep -ac 'malloc: ' "$log")
	armed=$(grep -ac 'C1-hunt: created' "$log")
	frames=$(grep -ao 'total [0-9]*)' "$log" | tail -1 | tr -dc 0-9)
	frames=${frames:-0}
	faults=$(grep -acE 'Exception|Data Abort|Fatal' "$log")
	if [ "$frames" -eq 0 ]; then
		verdict=VOID; void=$((void+1))
	else
		verdict=ok; valid=$((valid+1)); tot_f=$((tot_f+frames)); tot_fire=$((tot_fire+fires))
	fi
	# TDOWN: did the app run to completion and tear down? Measured 2026-09-25:
	# the two runs that reached STK's profile summary produced 79 of the 108
	# archived fires (78 in one), because the exit free-storm re-walks every heap
	# and re-detects corruption that happened earlier. It is a detection
	# AMPLIFIER, not a precondition -- 6 of the 8 firing runs never got there and
	# still fired 1-12 times. A bench of runs that all show no is not void, but it
	# is weaker than one that completes.
	if grep -aq 'Number of frames:' "$log"; then tdown=yes; else tdown=no; fi
	printf '%-38s %5s %6s %6s %8s %7s %5s %s\n' "$(basename "$log" .log | cut -c11-)" \
		"$fires" "$guards" "$armed" "$frames" "$faults" "$tdown" "$verdict"
done

echo "---"
printf 'valid trials: %s   void (0 frames, NOT counted): %s\n' "$valid" "$void"
printf 'C1-signature hits across valid trials: %s   frames: %s\n' "$tot_fire" "$tot_f"
