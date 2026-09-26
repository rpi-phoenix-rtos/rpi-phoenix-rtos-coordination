#!/bin/bash
# Read the C1 pacing line out of a trial: the heap-growth curve, the 90 s
# baselines, and (if the trial fired) where the fire sat on every axis at once.
#
# WHY. The archive says C1 is pinned to ~90 s after the first rendered frame and
# NOT to work done -- between two groups of runs whose elapsed time agrees to 3%
# the frame count at the fire differs by 1.9x. That was inferred from a frame
# counter that happens to be logged. `v3d-winsys: pace` now prints time, frames,
# BO creations, BO recycles and heap growth on one line, so the next fire can be
# placed on all of them directly. This is the reader for it.
#
# ★ IT IS ALSO USEFUL WITH NO FIRE AT ALL, which is the point. The pre-registered
# mechanism test is "does heapkb flatten at t ~ 76-90 s?" -- if STK's asset load
# finishes there, the onset is the moment allocation switches from growth to
# REUSE, which is what V3D_KEEP_CLOSED_BO=1 removes. One clean trial answers it.
#
# ⚠ TWO CLOCKS. `t=` here is continuous from the first flip; the archive's "90 s"
# is a sum of ~5 s flipstat bins up to the last bin BEFORE the hit. They can
# differ by most of a bin. Do not read a 5-8 s offset between them as a shift.
# For the same reason the pace line nearest a fire is up to 5 s stale.
#
# ⚠ `heaps=?` means libphoenix's weak malloc_c1Pacing did not resolve -- a stale
# libc, not an empty heap. Reported as a hard FAIL, because a `0` there would have
# been indistinguishable from "nothing allocated".
#
# Usage: scripts/c1-pace-read.sh <label|path>
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
art="$repo/artifacts/rpi4b-uart"
arg="${1:-}"

if [ -z "$arg" ]; then
	echo "usage: $(basename "$0") <label|path>" >&2
	exit 2
fi

if [ -f "$arg" ]; then
	log="$arg"
else
	shopt -s nullglob
	cand=($(ls -t "$art"/*"$arg"*.log 2>/dev/null))
	if [ ${#cand[@]} -eq 0 ]; then
		echo "no log matching '*${arg}*.log' under $art" >&2
		exit 2
	fi
	log="${cand[0]}"
fi

printf 'log: %s\n\n' "$(basename "$log")"

npace=$(grep -ac 'v3d-winsys: pace ' "$log")
if [ "$npace" -eq 0 ]; then
	echo "FAIL: no 'v3d-winsys: pace' line at all."
	echo "      Either the build predates the instrument or the port did not relink."
	echo "      Gate: strings /srv/phoenix-rpi4-nfs-gcc16/usr/bin/supertuxkart | grep -c 'v3d-winsys: pace'"
	echo "      (NOT bin/stk -- that is an 872 KB launcher with no winsys in it.)"
	exit 1
fi

if [ "$(grep -ac 'heaps=?' "$log")" -gt 0 ]; then
	echo "FAIL: pace lines report heaps=? -- libphoenix's weak malloc_c1Pacing did not resolve."
	echo "      The driver half shipped and the libc half did not. Rebuild --scope core."
	exit 1
fi

SIG='hhi32 = 0x0*8000000[01]|p4got  = 0x0*8000000[01]|= 0x8000000[01][0-9a-f]{8}'

# One pass: the curve, the 90 s baseline, and the last line before any fire.
awk -v sig="$SIG" '
	function fields(s,   a) {
		# t=<ms> frames=<n> boc=<n> bore=<n> vaa=<n> var=<n> heaps=<n> heapkb=<n>
		# ⚠ vaa/var are absent from the FIRST pacing build; treated as -1 so an
		# older log still reads rather than failing on a field that did not exist.
		t = ""; fr = ""; boc = ""; bore = ""; va = -1; vr = -1; hp = ""; hk = ""
		mu = -1; mk = -1; pl = -1
		if (match(s, /t=[0-9]+ms/))      t    = substr(s, RSTART + 2, RLENGTH - 4) + 0
		if (match(s, /frames=[0-9]+/))   fr   = substr(s, RSTART + 7, RLENGTH - 7) + 0
		if (match(s, /boc=[0-9]+/))      boc  = substr(s, RSTART + 4, RLENGTH - 4) + 0
		if (match(s, /bore=[0-9]+/))     bore = substr(s, RSTART + 5, RLENGTH - 5) + 0
		if (match(s, /vaa=[0-9]+/))      va   = substr(s, RSTART + 4, RLENGTH - 4) + 0
		if (match(s, /var=[0-9]+/))      vr   = substr(s, RSTART + 4, RLENGTH - 4) + 0
		if (match(s, /mun=[0-9]+/))      mu   = substr(s, RSTART + 4, RLENGTH - 4) + 0
		if (match(s, /munkb=[0-9]+/))    mk   = substr(s, RSTART + 6, RLENGTH - 6) + 0
		if (match(s, /pool=[0-9]+/))     pl   = substr(s, RSTART + 5, RLENGTH - 5) + 0
		if (match(s, /heaps=[0-9]+/))    hp   = substr(s, RSTART + 6, RLENGTH - 6) + 0
		if (match(s, /heapkb=[0-9]+/))   hk   = substr(s, RSTART + 7, RLENGTH - 7) + 0
		return (t != "" && hk != "")
	}
	{
		if (!fired && $0 ~ sig && n > 0) { fired = 1; fi = n }
		if ($0 ~ /v3d-winsys: pace /) {
			# ⚠ A corrupted line (this UART flips ~1.3%) fails the field match and is
			# dropped rather than parsed into a wrong number.
			if (!fields($0)) { bad++; next }
			n++
			T[n] = t; F[n] = fr; B[n] = boc; R[n] = bore; H[n] = hp; K[n] = hk
			VA[n] = va; VR[n] = vr; MU[n] = mu; MK[n] = mk; PL[n] = pl
		}
	}
	END {
		printf "pace lines: %d usable", n
		if (bad > 0) printf ", %d dropped as corrupt", bad
		printf "\n\n"
		if (n == 0) { print "no usable pace line"; exit 1 }

		print "  t(s)  frames    boc   bore    var    mun   munkb   pool  heapkb   d(heapkb)/dt"
		for (i = 1; i <= n; i++) {
			d = (i > 1 && T[i] > T[i-1]) ? (K[i] - K[i-1]) * 1000.0 / (T[i] - T[i-1]) : 0
			mark = ""
			if (fired && i == fi) mark = "   <== FIRE"
			printf "%6.1f %7d %6d %6d %6d %6d %7d %6d %7d %10.1f kB/s%s\n",
				T[i]/1000.0, F[i], B[i], R[i], VR[i], MU[i], MK[i], PL[i], K[i], d, mark
			if (VR[i] > 0 && vr0 == 0) vr0 = i
			if (R[i] > 0 && br0 == 0)  br0 = i
			if (MU[i] > 0 && mu0 == 0) mu0 = i
		}

		# The pre-registered 90 s baselines: nearest line to t = 90 s.
		best = 1; bd = 1e18
		for (i = 1; i <= n; i++) { dd = (T[i] > 90000) ? T[i] - 90000 : 90000 - T[i]; if (dd < bd) { bd = dd; best = i } }
		# The two recycling onsets, which is the discriminator KEEP_CLOSED_BO cannot give.
		printf "\nRECYCLING ONSETS (KEEP_CLOSED_BO=1 removes ALL THREE; these separate them):\n"
		if (MU[1] < 0) print "  ★ first page returned to the KERNEL (mun>0): field absent -- build predates it"
		else if (mu0 > 0) printf "  ★ first page returned to the KERNEL (mun>0): t = %.1f s\n", T[mu0]/1000.0
		else              print "  ★ first page returned to the KERNEL (mun>0): NEVER -- the boPool absorbed every"
		if (MU[1] >= 0 && mu0 == 0) print "     close, so no BO page reached malloc at all in this trial."
		if (br0 > 0) printf "  first PHYSICAL-frame reuse (bore>0): t = %.1f s\n", T[br0]/1000.0
		else         printf "  first PHYSICAL-frame reuse (bore>0): never in this trial\n"
		if (VR[1] < 0) print "  first GPU-VA reuse (var>0)        : field absent -- build predates the vaa/var counters"
		else if (vr0 > 0) printf "  first GPU-VA reuse (var>0)        : t = %.1f s\n", T[vr0]/1000.0
		else              printf "  first GPU-VA reuse (var>0)        : never in this trial\n"
		print "  -> the fire window opens at ~76 s of render. An onset that lands there is a"
		print "     candidate mechanism; one long past it is not. ★ `mun` is the one the C1"
		print "     signature REQUIRES: pooled pages never leave the driver, so they cannot end"
		print "     up under a malloc heap header. Only an unmapped one can."

		printf "\nBASELINE at t = %.1f s (nearest to 90 s):\n", T[best]/1000.0
		printf "  frames=%d  boc=%d  bore=%d  vaa=%d  var=%d  mun=%d  munkb=%d  pool=%d  heaps=%d  heapkb=%d\n",
			F[best], B[best], R[best], VA[best], VR[best], MU[best], MK[best], PL[best], H[best], K[best]
		printf "  -> bore here is what the on-fire read compares against. A fire well BELOW it\n"
		printf "     means the event lives in the FIRST recycles; at or above it, that framing is dead.\n"

		# The flatten test, stated as a number rather than an eyeball.
		# Growth rate over 40-75 s vs 95-130 s: the load phase against just after it.
		for (i = 1; i <= n; i++) {
			if (T[i] >= 40000 && T[i] <= 75000) { if (!a0) a0 = i; a1 = i }
			if (T[i] >= 95000 && T[i] <= 130000) { if (!b0) b0 = i; b1 = i }
		}
		if (a1 > a0 && b1 > b0) {
			ra = (K[a1] - K[a0]) * 1000.0 / (T[a1] - T[a0])
			rb = (K[b1] - K[b0]) * 1000.0 / (T[b1] - T[b0])
			printf "\nFLATTEN TEST (pre-registered): heap growth 40-75 s = %.1f kB/s, 95-130 s = %.1f kB/s\n", ra, rb
			if (ra > 0 && rb < ra / 3.0) {
				print "  => FLATTENS. The fire window opens where asset loading stops and reuse begins,"
				print "     which is what KEEP_CLOSED_BO=1 removes. The mechanism story survives."
			}
			else if (ra > 0) {
				print "  => DOES NOT FLATTEN. Heap growth runs straight through the window, so the"
				print "     \"loading ends, reuse begins\" story is dead and the ~90 s onset needs"
				print "     another cause. ★ Worth knowing before spending more trials on it."
			}
			else {
				print "  => growth was already flat before 75 s; the two windows cannot separate."
			}
		}
		else {
			print "\nFLATTEN TEST: not enough pace lines in 40-75 s and 95-130 s to compare."
		}

		if (fired) {
			printf "\nAT THE FIRE (last pace line before the signature, up to 5 s stale):\n"
			printf "  t=%.1f s  frames=%d  boc=%d  bore=%d  vaa=%d  var=%d  mun=%d  munkb=%d  pool=%d  heaps=%d  heapkb=%d\n",
				T[fi]/1000.0, F[fi], B[fi], R[fi], VA[fi], VR[fi], MU[fi], MK[fi], PL[fi], H[fi], K[fi]
			printf "  archive says t should be ~90 s. Compare frames and heapkb against the\n"
			printf "  clean-run spread, not against a number guessed beforehand.\n"
		}
		else {
			print "\n(no C1 signature in this trial -- the baselines above are the deliverable)"
		}
	}
' "$log"
