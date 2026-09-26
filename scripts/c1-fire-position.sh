#!/bin/bash
# WHERE IN A RUN DOES C1 FIRE? -- the hazard-shape test, from the archive alone.
#
# WHY THIS EXISTS. Every remaining lever on this hunt is "get more fires per
# hour", and the two candidate levers pointed in OPPOSITE directions depending on
# one unmeasured fact: is the hazard per-frame (longer runs amortise the ~5 min
# fixed cost) or per-boot (longer runs waste it)? Guessing wrong makes the bench
# slower for the rest of the hunt. 33 runs have already fired, and every log
# carries a periodic `v3d-winsys: flipstat … (total N)` line, so the position of
# the first signature hit is recoverable for free from data already on disk.
#
# ★ THE AXIS THAT MATTERS IS SECONDS SINCE THE FIRST FRAME -- and it took a wrong
# answer to find it. Frames settled the bench question (front-loaded, z = -4.3).
# STK then renders its menu/load phase at 0.1-0.9 fps for ~70 s before jumping to
# ~7.5 fps when the race starts, which made "seconds since the race starts" look
# like the natural clock, and on it the fires do cluster. ⚠ THAT AXIS IS WRONG.
# Race start is bimodal (70-74 s vs 80-84 s) on the SAME binary, which splits the
# archive into two groups and asks which axis stays put:
#
#   time since first frame   93.3 s vs 90.6 s   <- invariant (t = 0.66, ns)
#   time since race start     +22 s vs  +7.8 s  <- moves (t ~ 3.5)
#   frames rendered             244 vs 131      <- moves (1.9x)
#
# So C1 is NOT paced by frames. ⚠ That is not the same as "not paced by work",
# which an earlier version of this header claimed: group B spends its extra ~12 s
# in the sub-1-fps ASSET-LOAD phase, which allocates heavily while rendering
# almost nothing, so the two groups can have similar ALLOCATION counts despite the
# 1.9x frame difference. Elapsed time and allocation count are not separable from
# anything in these logs -- which is why the `v3d-winsys: pace` line now carries
# heaps/heapkb/boc, and why scripts/c1-pace-read.sh exists.
# All three axes are printed, because the wrong ones are what rule the right one in.
#
# ⚠ UART CORRUPTION. This link flips ~1.3% of lines, and one flipped digit in
# `total N` invents a frame count that never happened -- which in a MAX would
# silently become the run length. Totals are monotonic and flipstat prints every
# few seconds, so a genuine step is bounded; the parser accepts a new total only
# if it is >= the last one and no more than MAXJUMP above it, and an interval only
# if it is under a minute. That rejects both a high-bit flip and a dropped digit
# without needing to know which happened.
#
# ⚠ RESOLUTION IS THE FLIPSTAT BIN (~5 s). Positions are the accumulated time at
# the last flipstat BEFORE the hit, so they quantise to ~5 s steps. Do not read
# structure finer than that -- the apparent "clusters" at exact multiples of 5 are
# the bins, not the phenomenon.
#
# Usage: scripts/c1-fire-position.sh [label-substring]
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
art="$repo/artifacts/rpi4b-uart"
pref="${1:-}"

SIG='hhi32 = 0x0*8000000[01]|p4got  = 0x0*8000000[01]|= 0x8000000[01][0-9a-f]{8}'
MAXJUMP=2000    # flipstat cadence is seconds at <=60 fps; a real step never nears this
RACEFPS=3       # the menu/load phase runs under 1 fps, the race at ~7.5 -- 3 separates them

shopt -s nullglob
logs=("$art"/*"$pref"*.log)
[ ${#logs[@]} -eq 0 ] && { echo "no logs matching '*${pref}*.log'" >&2; exit 1; }

printf '%-30s %7s %7s %6s %9s %8s\n' RUN 'FIRE@fr' TOTALfr 'FRAC%' 'FIRE@s' 'POST-RACE'

tmp=$(mktemp); rel=$(mktemp); abs=$(mktemp); grp=$(mktemp)
trap 'rm -f "$tmp" "$rel" "$abs" "$grp"' EXIT

for log in $(printf '%s\n' "${logs[@]}" | sort); do
	grep -aqE "$SIG" "$log" || continue
	out=$(awk -v sig="$SIG" -v maxjump="$MAXJUMP" -v racefps="$RACEFPS" '
		{
			if (!fired && $0 ~ sig) { fired = 1; ffr = fr; fms = ms }
			if (match($0, /flipstat [0-9]+ frames in [0-9]+ ms = [0-9.]+ fps/)) {
				split(substr($0, RSTART, RLENGTH), a, " ")
				d = a[5] + 0
				if (d > 0 && d < 60000) {
					ms += d
					# First bin above the menu/load floor = the race starting.
					if (race == 0 && a[8] + 0 > racefps) race = ms
				}
			}
			if (match($0, /total [0-9]+\)/)) {
				v = substr($0, RSTART + 6, RLENGTH - 7) + 0
				if (v >= fr && v <= fr + maxjump) fr = v
			}
		}
		END { printf "%d %d %d %d %d %d\n", fired, ffr, fr, fms, ms, race }
	' "$log")
	set -- $out
	fired=$1 ffr=$2 totfr=$3 fms=$4 totms=$5 race=$6
	[ "$fired" -eq 0 ] && continue
	[ "$totfr" -le 0 ] && continue
	frac=$(awk -v f="$ffr" -v t="$totfr" 'BEGIN{printf "%.1f", 100*f/t}')
	if [ "$race" -gt 0 ]; then
		post=$(awk -v f="$fms" -v r="$race" 'BEGIN{printf "%+.1f", (f-r)/1000}')
		echo "$post" >> "$rel"
		# race-seconds, abs-seconds, race-relative, frames -- for the bimodal split
		awk -v r="$race" -v f="$fms" -v p="$post" -v n="$ffr" \
			'BEGIN{printf "%.1f %.1f %s %d\n", r/1000, f/1000, p, n}' >> "$grp"
	else
		post="-"
	fi
	printf '%-30s %7s %7s %6s %8.1fs %8s\n' \
		"$(basename "$log" .log | cut -c17-)" "$ffr" "$totfr" "$frac" \
		"$(awk -v v="$fms" 'BEGIN{printf "%.1f", v/1000}')" "$post"
	echo "$frac" >> "$tmp"
	awk -v v="$fms" 'BEGIN{printf "%.1f\n", v/1000}' >> "$abs"
done

echo "---"
[ -s "$tmp" ] || { echo "no firing run had a usable frame total"; exit 0; }

echo "AXIS 1 -- position in the run, as a fraction of frames rendered"
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
		printf "  firing runs: %d   mean position %.1f%% (uniform null 50.0%%, se %.1f%%)   z = %+.2f\n", n, mean, se, z
		printf "  sample sd %.1f%% (uniform null 28.9%%)\n", sd
		for (i = 1; i <= n; i++) { if (v[i] < 20) lo++; if (v[i] >= 80) hi++ }
		printf "  first 20%% of a run: %d    last 20%%: %d    (uniform expects %.1f each)\n", lo+0, hi+0, n/5
		if (z < -2)      print "  => FRONT-LOADED: the hazard is NOT uniform in frames."
		else if (z > 2)  print "  => BACK-LOADED: fires cluster late; something accumulates."
		else             print "  => consistent with a CONSTANT PER-FRAME hazard."
		print "  ⚠ This axis cannot say WHY. A per-boot event and a per-allocation one whose"
		print "    allocations are front-loaded both look like this. Read axis 2 for the shape."
	}
' "$tmp"

echo
echo "AXIS 2 -- seconds since the FIRST FRAME  ★ the invariant axis"
awk '
	{ v[NR] = $1 + 0 }
	END {
		n = NR
		for (i = 1; i <= n; i++) for (j = i + 1; j <= n; j++) if (v[j] < v[i]) { t = v[i]; v[i] = v[j]; v[j] = t }
		printf "  n = %d   min %.1fs   median %.1fs   max %.1fs\n", n, v[1], v[int((n+1)/2)], v[n]
		for (i = 1; i <= n; i++) if (v[i] >= 70 && v[i] <= 115) w++
		printf "  in 70..115 s after the first frame: %d of %d (%.0f%%)\n", w+0, n, 100*w/n
		print "  histogram (20 s bins since the first frame):"
		for (i = 1; i <= n; i++) { b = int(v[i] / 20); h[b]++; if (b > mx) mx = b }
		for (b = 0; b <= mx; b++) {
			bar = ""; for (k = 0; k < h[b]; k++) bar = bar "#"
			printf "    %4d..%4d s  %-24s %s\n", b * 20, b * 20 + 19, bar, (h[b] ? h[b] : "")
		}
	}
' "$abs"

echo
echo "THE SPLIT THAT PICKS THE AXIS -- race start is bimodal, so ask which axis stays put"
echo "  (in-window fires only: -15s..+60s of race start)"
# $1 >= 40 keeps this to the STK workload: the one archived quake3 fire races at
# 5 s and would otherwise land in group A and drag its mean.
awk '$3 <= 60 && $3 >= -15 && $1 >= 40 {
		g = ($1 < 76) ? "A race~70" : "B race~82"
		n[g]++; a[g] += $2; r[g] += $3; f[g] += $4
	}
	END {
		printf "  %-10s %4s %14s %14s %10s\n", "GROUP", "n", "SINCE-FRAME-1", "SINCE-RACE", "FRAMES"
		for (g in n) printf "  %-10s %4d %12.1f s %12.1f s %10.0f\n", g, n[g], a[g]/n[g], r[g]/n[g], f[g]/n[g]
		print "  => the row that barely moves is the anchor. Frames and race-relative both move ~2x."
	}
' "$grp"

# Stronger than the buckets, and on the same data: regress the fire time on the
# race start. Slope 1 = the fires follow race start; slope 0 = they are pinned to
# the first frame. Bucketing throws away the within-group spread; this does not.
awk '$3 <= 60 && $3 >= -15 && $1 >= 40 { n++; x[n] = $1; y[n] = $2; sx += $1; sy += $2 }
	END {
		if (n < 4) { print "  (too few in-window fires to regress)"; exit }
		mx = sx / n; my = sy / n
		for (i = 1; i <= n; i++) { dx = x[i] - mx; sxx += dx * dx; sxy += dx * (y[i] - my) }
		b = sxy / sxx; a = my - b * mx
		for (i = 1; i <= n; i++) { r = y[i] - (a + b * x[i]); sse += r * r }
		se = sqrt(sse / (n - 2) / sxx)
		printf "\n  regression of fire time on race start (n = %d): slope %+.3f (se %.3f)\n", n, b, se
		printf "    vs slope 1 -- fires FOLLOW race start  : t = %+.2f\n", (b - 1) / se
		printf "    vs slope 0 -- fires pinned to FRAME 1  : t = %+.2f\n", b / se
		printf "    intercept %.1f s after the first frame\n", a
	}
' "$grp"

echo
echo "AXIS 3 -- seconds since the race starts  ⚠ REFUTED as the anchor, printed to keep it refuted"
if [ ! -s "$rel" ]; then
	echo "  no run had an identifiable race start -- axis unavailable"
	exit 0
fi
awk '
	{ v[NR] = $1 + 0 }
	END {
		n = NR
		for (i = 1; i <= n; i++)
			for (j = i + 1; j <= n; j++)
				if (v[j] < v[i]) { t = v[i]; v[i] = v[j]; v[j] = t }
		printf "  n = %d   min %+.1fs   median %+.1fs   max %+.1fs\n", n, v[1], v[int((n+1)/2)], v[n]
		for (i = 1; i <= n; i++) if (v[i] <= 40 && v[i] >= -10) w++
		printf "  within -10s..+40s of race start: %d of %d (%.0f%%) -- looks tight, but see AXIS 2:\n", w+0, n, 100*w/n
		print "  race start is bimodal (70-74 s vs 80-84 s) and the fires do NOT follow it."
		print "  histogram (20 s bins since race start):"
		for (i = 1; i <= n; i++) { b = int((v[i] + 20) / 20); if (b < 0) b = 0; h[b]++ ; if (b > mx) mx = b }
		for (b = 0; b <= mx; b++) {
			lo = b * 20 - 20; bar = ""
			for (k = 0; k < h[b]; k++) bar = bar "#"
			printf "    %+5d..%+5d s  %-24s %s\n", lo, lo + 19, bar, (h[b] ? h[b] : "")
		}
	}
' "$rel"
