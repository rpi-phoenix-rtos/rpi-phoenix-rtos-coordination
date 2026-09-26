#!/bin/bash
# Warn BEFORE a bench runs that its label collides with archived logs.
#
# WHY. Grading scripts match a label as a SUBSTRING, so a new label that is a
# prefix of (or shares a prefix with) an old one silently aggregates unrelated
# runs into the verdict. This has now cost real time twice:
#
#   "c1s"  also matched c1stk / c1sweep / c1solo / c1soak / c1smoke, turning a
#          1-trial series into a 19-trial aggregate across three days.
#   "c1bo" also matched c1both-t1 and c1body-t1..t7 from 2026-09-23, so a 6-trial
#          series reported "9 / 6 logs" the moment it started.
#
# Both were caught only when the numbers looked wrong. Catching it at LAUNCH is
# free; catching it at grading time means re-reading a verdict you already formed.
#
# ⚠ Checks BOTH directions, because either one aggregates:
#   - existing logs whose label CONTAINS the new one  (old c1body vs new "c1bo")
#   - existing logs whose label IS CONTAINED BY it    (old "c1bo" vs new c1bo1)
#
# Usage: scripts/check-label-free.sh <label> [<label> ...]
# Exit:  0 = every label is clean, 1 = at least one collides.
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
art="$repo/artifacts/rpi4b-uart"

if [ $# -eq 0 ]; then
	echo "usage: $(basename "$0") <label> [<label> ...]" >&2
	exit 2
fi

shopt -s nullglob
rc=0

for label in "$@"; do
	hits=()
	for f in "$art"/*.log; do
		# The label as the harness writes it: rpi4b-uart-<ts>-<label>.log
		base=$(basename "$f" .log)
		lab=${base#rpi4b-uart-????????-??????-}
		[ "$lab" = "$base" ] && continue
		case "$lab" in
			*"$label"*) hits+=("$lab") ;;              # old log would match a grep for the new label
			*) case "$label" in *"$lab"*) hits+=("$lab") ;; esac ;;  # new label would match a grep for the old
		esac
	done
	if [ ${#hits[@]} -eq 0 ]; then
		printf '✓ %-14s free\n' "$label"
	else
		rc=1
		printf '⚠ %-14s COLLIDES with %s archived log(s):\n' "$label" "${#hits[@]}"
		printf '%s\n' "${hits[@]}" | sort -u | sed 's/^/    /' | head -8
		printf '    -> grading by this label will aggregate them; pick a distinctive one.\n'
	fi
done

exit $rc
