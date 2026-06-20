#!/usr/bin/env bash
# coldstate-correlate.sh — render-stall STEP-3 discriminator analysis (task #13/#16).
#
# Pairs each cold boot's V3D cold-power-on state (the "v3d-coldstate:" line printed by
# v3d_phoenix_logColdState at winsys init) with that boot's stall verdict (did the GPU
# render thread time out?), so a stalled boot's line can be diffed against a clean one.
# The advisor's point: a register that cleanly separates the STALL vs CLEAN populations is
# the deterministic root cause — found in far fewer boots than an underpowered rate A/B.
#
# Usage: ./scripts/coldstate-correlate.sh <label-prefix>   (e.g. coldstate-A, or just coldstate)
# Scans artifacts/rpi4b-uart/*<prefix>*.log.
set -u
prefix="${1:-coldstate}"
dir="artifacts/rpi4b-uart"
printf '%-44s %-7s %s\n' "LOG" "VERDICT" "COLDSTATE"
printf '%-44s %-7s %s\n' "---" "-------" "---------"
for f in "$dir"/*"$prefix"*.log; do
	[ -e "$f" ] || continue
	cs=$(grep -a -m1 "v3d-coldstate: clk_v3d" "$f" | sed 's/^.*v3d-coldstate: //')
	# A boot is STALLED if the winsys logged at least one render timeout; CLEAN if it
	# rendered and never timed out; NORENDER if quake never reached first-frame (no coldstate).
	if [ -z "$cs" ]; then
		verdict="NOREND"
	elif grep -aq "RENDER TIMEOUT" "$f"; then
		verdict="STALL"
	else
		verdict="CLEAN"
	fi
	printf '%-44s %-7s %s\n' "$(basename "$f")" "$verdict" "${cs:-<no coldstate line>}"
done
echo
echo "# Counts:"
echo "#   boots with coldstate: tally STALL vs CLEAN lines above and diff the register columns."
echo "#   (clk_v3d cfg/meas/delta, clkstate, temp, throttled, corevolt, pm_grafx)"
