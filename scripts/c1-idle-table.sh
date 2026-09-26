#!/bin/bash
# Grade the paired idle-vs-back-to-back-vs-KEEP series in one command.
#
# WHY A DEDICATED READER. `c1-bench-table.sh` already prints the schedule MODE
# per trial, but this series' whole point is the 2x2 of ARM against MODE, and the
# arm is encoded in the label (I / B / K). Reading that by eye across nine trials
# is exactly where a mixed result gets rounded to whichever answer was expected.
#
# THE QUESTION. C1's fire rate is 35.3% after an idle >300 s and 7.1%
# back-to-back (p = 1.6e-06) -- but in the archive nearly every long gap IS a
# build, so "the bench was idle" and "the binary was freshly built" have never
# been separated. This series idles with no build. The primary endpoint is the
# MODE, not the fire: n is far too small for a 35%-vs-7% rate, but every trial
# reports its mode whether it fires or not.
#
# ⚠ THE K ARM NEEDS ITS OWN GUARD. V3D_KEEP_CLOSED_BO=1 is passed through psh ->
# /usr/bin/env -> the stk launcher -> execv. If any link drops it the trial runs
# as a DEFAULT trial and its clean result would be read as suppression. The
# banner `V3D_KEEP_CLOSED_BO=1 -- not unmapping closed BOs` is the only proof the
# arm was armed; a K trial without it is VOID, and this prints it as VOID rather
# than as a clean zero.
#
# Usage: scripts/c1-idle-table.sh [label-prefix] [series-driver-log]
#          default prefix c1idle; the driver log adds the CACHE column, which is
#          host-side and therefore absent from every UART log.
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# ⚠ Overridable ONLY so a synthetic test set never has to be copied into the real
# archive. Validating this script the obvious way -- dropping nine fake logs into
# artifacts/rpi4b-uart/ -- puts fabricated trials where every future archive-wide
# query (fire rates, schedule modes, race starts) would silently count them. They
# were removed immediately, but the right fix is to make the directory a knob:
#   C1_ART_DIR=/tmp/fake scripts/c1-idle-table.sh <prefix>
art="${C1_ART_DIR:-$repo/artifacts/rpi4b-uart}"
pref="${1:-c1idle}"
# Optional: the SERIES driver log, which is the only place the cycle's
# `cleared`/`KEPT` line lives. Without it the CACHE column reads "-".
drv="${2:-}"

SIG='hhi32 = 0x0*8000000[01]|p4got  = 0x0*8000000[01]|= 0x8000000[01][0-9a-f]{8}'

shopt -s nullglob
logs=("$art"/*"$pref"*.log)
[ ${#logs[@]} -eq 0 ] && { echo "no logs matching '*${pref}*.log'" >&2; exit 1; }

printf '%-30s %-4s %-6s %-6s %8s %6s %5s %5s %s\n' RUN ARM CACHE MODE 'RACE@s' FRAMES KFLT FIRES NOTE

declare -A n_mode n_fire
void=0

for log in $(printf '%s\n' "${logs[@]}" | sort); do
	base=$(basename "$log" .log)
	lab=${base##*-}
	case "$lab" in
		*I[0-9]) arm=I ;;
		*B[0-9]) arm=B ;;
		*K[0-9]) arm=K ;;
		*C[0-9]) arm=C ;;   # c1cc: shader cache deliberately CLEARED before this trial
		*W[0-9]) arm=W ;;   # c1cc: cache left alone (WARM -- rebuilt by the C trial)
		*)       arm=? ;;
	esac

	# ★ The cache state is HOST-side: the cycle prints `cleared` or `KEPT` into the
	# SERIES driver log, never into the UART. Without it the arm is graded by the
	# label alone -- i.e. by what the script INTENDED, not by what happened, and a
	# failed `rm` would read as a clean cold trial. Paired by the `log:` line the
	# cycle prints immediately after, so this does not rely on ordering.
	cache="-"
	if [ -n "$drv" ] && [ -f "$drv" ]; then
		# ⚠ WHEN THE CACHE DIRECTORY IS ABSENT THE SYNC PRINTS NOTHING AT ALL.
		# sync-netboot-tree.sh guards its whole message on `if [ -d $shader_cache ]`,
		# so a trial whose cache was deliberately `rm -rf`ed produces NO line -- and
		# grading on the message alone would read the coldest possible trial as
		# "unknown". The series script's own `cleared at HH:MM:SS` covers that case;
		# whichever marker came last before this trial's `log:` line wins.
		cache=$(awk -v want="$base" '
			/^cleared at /            { st = "COLD" }
			/Mesa shader disk cache/  { st = (/cleared/) ? "COLD" : "warm" }
			/rpi4b-uart-.*\.log/ {
				if (index($0, want) > 0 && st != "") { print st; exit }
			}' "$drv")
		cache=${cache:--}
	fi

	race=$(awk '{ if (match($0, /flipstat [0-9]+ frames in [0-9]+ ms = [0-9.]+ fps/)) {
			split(substr($0, RSTART, RLENGTH), a, " "); d = a[5] + 0
			if (d > 0 && d < 60000) { ms += d; if (r == 0 && a[8] + 0 > 3) r = ms } } }
		END { printf "%.1f", r / 1000 }' "$log")
	mode=$(awk -v r="$race" 'BEGIN { print (r <= 0) ? "-" : (r >= 76 ? "LATE" : "early") }')
	frames=$(grep -ao 'total [0-9]*)' "$log" | tr -dc '0-9 \n' | tail -1)
	frames=${frames:-0}
	fires=$(grep -acE "$SIG" "$log")
	# EL1 entries only, and NOT halved: every EL0 dump reaches the UART twice but
	# an EL1 one does not, and halving a kernel fault could round it to zero.
	kflt=$(grep -acE 'Exception #[0-9]+ .*EL1|Data Abort.*EL1' "$log")
	# Total BO unmaps at the last pace line: 0 is the signature of an armed K trial.
	kmun=$(grep -ao 'munc=[0-9]* munp=[0-9]*' "$log" | tail -1 | tr -dc '0-9 ' | awk '{print ($1 + $2) + 0}')
	kmun=${kmun:-0}

	note=""
	if [ "$frames" -eq 0 ]; then
		note="VOID (0 frames)"
		void=$((void + 1))
	elif [ "$arm" = "K" ] && [ "$(grep -ac 'not unmapping closed BOs' "$log")" -eq 0 ]; then
		# ⛔ Not a clean K result -- the arm never armed.
		#
		# ⚠ MATCH THE DRIVER'S BANNER, NOT THE VARIABLE NAME. The first version
		# grepped for `V3D_KEEP_CLOSED_BO=1`, which also matches psh's echo of the
		# command line -- so it passed on the string the SHELL printed rather than
		# the one the driver printed, and would have graded a trial where the env
		# var never reached the winsys as properly armed. Measured on c1idleK1:
		# 2 matches for the variable name, only one of them the driver's.
		note="VOID (K arm NOT armed: no driver banner)"
		void=$((void + 1))
	elif [ "$arm" = "C" ] && [ "$cache" = "warm" ]; then
		# ⛔ A cold-arm trial whose cache was warm: the rm failed, or the sync kept
		# it. Not a cold datum, and a clean result from it would be read as the
		# cold arm failing to fire.
		note="VOID (C arm but cache was warm)"
		void=$((void + 1))
	elif [ "$arm" = "W" ] && [ "$cache" = "COLD" ]; then
		note="VOID (W arm but cache was COLD)"
		void=$((void + 1))
	elif [ "$arm" = "K" ] && [ "$kmun" -ne 0 ]; then
		# ★ The MECHANISTIC guard, which is stronger than any banner: with
		# KEEP_CLOSED_BO=1 the driver never unmaps, so munc+munp must be 0. A K
		# trial that unmapped anything was not running the arm it claims to,
		# whatever the banner says.
		note="VOID (K arm banner present but munc+munp=$kmun, not 0)"
		void=$((void + 1))
	else
		key="$arm $mode"
		n_mode[$key]=$(( ${n_mode[$key]:-0} + 1 ))
		[ "$fires" -gt 0 ] && n_fire[$key]=$(( ${n_fire[$key]:-0} + 1 ))
		[ "$fires" -gt 0 ] && note="FIRED"
	fi

	printf '%-30s %-4s %-6s %-6s %8s %6s %5s %5s %s\n' \
		"$(echo "$base" | cut -c17-)" "$arm" "$cache" "$mode" "$race" "$frames" "$kflt" "$fires" "$note"
done

echo "---"
printf '%-6s %6s %6s %8s\n' ARM LATE early 'fired'
for a in I B K C W; do
	l=${n_mode[$a LATE]:-0}; e=${n_mode[$a early]:-0}
	f=$(( ${n_fire[$a LATE]:-0} + ${n_fire[$a early]:-0} ))
	printf '%-6s %6s %6s %8s\n' "$a" "$l" "$e" "$f"
done
[ "$void" -gt 0 ] && printf 'void trials (NOT counted above): %s\n' "$void"

echo "---"
echo "PRE-REGISTERED READ (docs/inprogress/WEEK-2026-W39.md):"
echo "  I mostly LATE  + B mostly early -> the IDLE causes the mode; schedule rule established."
echo "  I mostly early + B mostly early -> it was NEVER the idle. Leading replacement is CACHE"
echo "                                     EVICTION by the build, not a fresh binary -- next test"
echo "                                     is to drop the host caches during the idle, no build."
echo "  both arms LATE                  -> something moved the whole series; the comparison is"
echo "                                     VOID, not positive. Re-run."
echo "  mixed                           -> a dose result. Report the split and raise IDLE before"
echo "                                     concluding. 4.7% of back-to-back trials are late anyway,"
echo "                                     so one discordant trial in three is expected."
echo "  K arm: fires at ~the I rate     -> the KEEP_CLOSED_BO demotion is confirmed."
echo "         0 fires vs I firing      -> first real evidence it does something beyond schedule."
