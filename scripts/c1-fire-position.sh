#!/bin/bash
# WHERE IN A RUN DOES C1 FIRE? -- the hazard-shape test, from the archive alone.
#
# WHY THIS EXISTS. Every remaining lever on this hunt is "get more fires per
# hour", and the two candidate levers point in OPPOSITE directions depending on
# one unmeasured fact:
#
#   - If the hazard is PER-FRAME (a corrupting store that any frame can make),
#     then longer runs per boot are strictly better: the ~50 s of boot overhead
#     is amortised over more exposure. Four laps instead of one buys ~+32%
#     exposure/hour.
#   - If the hazard is PER-BOOT (something that happens once, during startup or
#     the first allocations, and is merely *detected* later), then longer runs
#     buy nothing and cost boots: four laps gives 2.88 boots/h against 4.35, a
#     34% LOSS.
#
# Guessing wrong makes the bench slower for the rest of the hunt. The archive can
# answer it for free: 33 runs have already fired, and each log carries a periodic
# `flipstat ... (total N)` line, so the frame index at the moment of the first
# signature hit is recoverable without running anything.
#
# THE TEST. Under a constant per-frame hazard the first hit lands uniformly in
# the run, so the fraction idx/total is ~U(0,1) with mean 0.5 (sd 0.289, and with
# n=33 the standard error is 0.050 -- enough to see a mean of 0.40 at 2 sigma).
# A per-boot event instead piles the fractions up near 0.
#
# ⚠ UART CORRUPTION. This link flips ~1.3% of lines, and one flipped digit in
# `total N` invents a frame count that never happened -- which in a MAX would
# silently become the run length. Totals are monotonic and flipstat prints every
# few seconds, so a genuine step is bounded; the parser accepts a new total only
# if it is >= the last one and no more than MAXJUMP above it, and ignores
# everything else. That rejects both a high-bit flip and a digit dropped from the
# middle, without needing to know which happened.
#
# Usage: scripts/c1-fire-position.sh [label-substring]
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
art="$repo/artifacts/rpi4b-uart"
pref="${1:-}"

SIG='hhi32 = 0x0*8000000[01]|p4got  = 0x0*8000000[01]|= 0x8000000[01][0-9a-f]{8}'
MAXJUMP=2000   # flipstat cadence is seconds at <=60 fps; a real step never nears this

shopt -s nullglob
logs=("$art"/*"$pref"*.log)
[ ${#logs[@]} -eq 0 ] && { echo "no logs matching '*${pref}*.log'" >&2; exit 1; }

printf '%-34s %8s %8s %7s\n' RUN 'FIRE@' TOTAL 'FRAC%'

tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT

for log in $(printf '%s\n' "${logs[@]}" | sort); do
	grep -aqE "$SIG" "$log" || continue
	read -r fire total < <(awk -v sig="$SIG" -v maxjump="$MAXJUMP" '
		{
			if (!fired && $0 ~ sig) { fired = 1; fire = last }
			if (match($0, /total [0-9]+\)/)) {
				v = substr($0, RSTART + 6, RLENGTH - 7) + 0
				# monotonic + bounded step: rejects a corrupted digit either way
				if (v >= last && v <= last + maxjump) last = v
			}
		}
		END { if (!fired) fire = -1; printf "%d %d\n", fire, last }
	' "$log")
	[ "$total" -le 0 ] && continue
	[ "$fire" -lt 0 ] && continue
	frac=$(awk -v f="$fire" -v t="$total" 'BEGIN{printf "%.1f", 100*f/t}')
	printf '%-34s %8s %8s %7s\n' "$(basename "$log" .log | cut -c17-)" "$fire" "$total" "$frac"
	echo "$frac" >> "$tmp"
done

echo "---"
[ -s "$tmp" ] || { echo "no firing run had a usable frame total"; exit 0; }

awk '
	{ v[NR] = $1; s += $1 }
	END {
		n = NR; mean = s / n
		for (i = 1; i <= n; i++) { d = v[i] - mean; ss += d * d }
		sd = (n > 1) ? sqrt(ss / (n - 1)) : 0
		# Null: U(0,1) scaled to percent -> mean 50, sd 28.9. Use the THEORETICAL
		# sd for the standard error: the sample sd is itself the thing under test
		# (a front-loaded hazard shrinks it), so using it would hide the effect.
		se = 28.9 / sqrt(n)
		z = (mean - 50) / se
		printf "firing runs with a position: %d\n", n
		printf "mean position in run       : %.1f%%  (uniform null = 50.0%%)\n", mean
		printf "sample sd                  : %.1f%%  (uniform null = 28.9%%)\n", sd
		printf "z vs the uniform null      : %+.2f  (se %.1f%%)\n", z, se
		# Count the first and last fifth -- the shape, not just the mean.
		for (i = 1; i <= n; i++) { if (v[i] < 20) lo++; if (v[i] >= 80) hi++ }
		printf "in the first 20%% of a run  : %d   in the last 20%%: %d   (uniform expects %.1f each)\n", lo+0, hi+0, n/5
		print "---"
		if (z < -2) {
			print "verdict: FRONT-LOADED -- the fires cluster early. A per-boot event that is"
			print "         merely detected later. Longer runs buy nothing; prefer MORE BOOTS."
		} else if (z > 2) {
			print "verdict: BACK-LOADED -- fires cluster late. Something accumulates; longer"
			print "         runs are worth more than their frame count alone suggests."
		} else {
			print "verdict: consistent with a CONSTANT PER-FRAME hazard (uniform). Exposure is"
			print "         frames, so amortising boot overhead over longer runs is a real gain."
		}
	}
' "$tmp"
