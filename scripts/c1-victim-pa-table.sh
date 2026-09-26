#!/bin/bash
# Cross-run table of C1 victim addresses: virtual, physical, and poison-break pages.
#
# WHY THIS EXISTS. The single-run report answers "did this run's corrupted page
# match a mailbox buffer". The question that decides the NEXT instrument is
# different and only visible across runs: do the victims cluster?
#
#   - If the victim's PHYSICAL page falls in a predictable band, the A72 store
#     watchpoint (pctl_watchpoint, with its [trapLo,trapHi) value filter) becomes
#     usable: it has ONE comparator, so it is only worth arming when you can guess
#     the page. Today that is a ~1-in-95 shot per run.
#   - If the victims are scattered, the watchpoint stays the wrong tool and the
#     effort belongs on attribution (was the page a closed BO?) instead.
#
# ⚠ MAJORITY VOTE, NOT `sort -u`. This serial link corrupts ~1.3% of lines, and a
# single flipped hex digit invents an address that never existed -- on run c1coin
# `sort -u` reported two distinct `hpa` values where the true split was 50 vs 1.
# Every field here is reduced by frequency, and the runner-up is shown with its
# count so a genuine second value is never hidden by the same rule.
#
# That the COUNT RATIO separates the two cases is not theoretical -- both appear in
# the archive and the display tells them apart at a glance:
#   c1coin  hpa  0x8112000(n=50, 2nd=1x  0x8102000)  -> 50:1, the runner-up is a bit-error
#   c1pa1   heap 0xbd1f000(n=58, 2nd=56x 0xbd60000)  -> 58:56, TWO genuine victim heaps
#
# Usage: scripts/c1-victim-pa-table.sh <label-prefix>
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

# Majority value of a field, plus how many distinct values were seen and what the
# runner-up was. Prints: "<value> (n=<count>)" or "-" when the field never appeared.
_vote() {
	local log="$1" re="$2"
	local top cnt distinct second
	top=$(grep -aoE "$re" "$log" | grep -oE '0x[0-9a-f]+' | sed 's/^0x0*/0x/' | sort | uniq -c | sort -rn | head -1)
	[ -z "$top" ] && { printf -- '-'; return; }
	cnt=$(echo "$top" | awk '{print $1}')
	top=$(echo "$top" | awk '{print $2}')
	distinct=$(grep -aoE "$re" "$log" | grep -oE '0x[0-9a-f]+' | sed 's/^0x0*/0x/' | sort -u | wc -l)
	printf '%s(n=%s' "$top" "$cnt"
	if [ "$distinct" -gt 1 ]; then
		second=$(grep -aoE "$re" "$log" | grep -oE '0x[0-9a-f]+' | sed 's/^0x0*/0x/' \
			| sort | uniq -c | sort -rn | sed -n 2p | awk '{print $1"x "$2}')
		printf ',2nd=%s' "$second"
	fi
	printf ')'
}

printf '%-30s %5s %-26s %-22s %s\n' RUN SIG 'VICTIM VA (majority)' 'VICTIM PA hpa' 'POISON PAGES p4pa'
fired=0; total=0
allpa=""
for log in $(printf '%s\n' "${logs[@]}" | sort); do
	sig=$(grep -acE 'hhi32 = 0x0*8000000[01]|p4got  = 0x0*8000000[01]|= 0x8000000[01][0-9a-f]{8}' "$log")
	total=$((total + 1))
	[ "$sig" -gt 0 ] && fired=$((fired + 1))
	va=$(_vote "$log" 'heap  = 0x[0-9a-f]{16}')
	pa=$(_vote "$log" 'hpa   = 0x[0-9a-f]{16}')
	# p4pa is genuinely multi-valued (one per break), so list it rather than vote.
	p4=$(grep -aoE 'p4pa   = 0x[0-9a-f]{16}' "$log" | sed 's/.*0x0*/0x/' | sort -u | tr '\n' ' ')
	printf '%-30s %5s %-26s %-22s %s\n' "$(basename "$log" .log | cut -c17-)" "$sig" "$va" "$pa" "${p4:--}"
	case "$pa" in -) ;; *) allpa="$allpa $(echo "$pa" | sed 's/(.*//')";; esac
done

echo "---"
printf 'runs: %s   fired: %s\n' "$total" "$fired"
if [ -n "${allpa// /}" ]; then
	echo "victim physical pages across firing runs:$allpa"
	# The clustering verdict the next instrument choice depends on. A 1 MiB span is
	# the threshold because the watchpoint needs a page guess, not a region.
	lo=$(for p in $allpa; do echo $((p)); done | sort -n | head -1)
	hi=$(for p in $allpa; do echo $((p)); done | sort -n | tail -1)
	span=$(( (hi - lo) / 1024 ))
	printf 'span: %s KiB  (lo=0x%x hi=0x%x)\n' "$span" "$lo" "$hi"
	if [ "$(echo $allpa | wc -w)" -lt 2 ]; then
		echo "verdict: only one firing run -- no clustering claim is possible yet"
	elif [ "$span" -lt 1024 ]; then
		echo "verdict: CLUSTERED within 1 MiB -- a watchpoint page guess becomes plausible"
	else
		echo "verdict: SCATTERED over $span KiB -- the single-comparator watchpoint stays the wrong tool"
	fi
fi
