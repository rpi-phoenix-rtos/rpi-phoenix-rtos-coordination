#!/usr/bin/env bash
#
# reliability-tally.sh — recompute the system-level boot-reliability figure from
# the UART logs already on disk.
#
# Why this is a script and not a one-off command: the figure quoted in
# docs/PHOENIX-RTOS-RPI4-CHANGES.md ("132 boots, 132 reached the prompt — 100.0%,
# 17 fault-bearing, all attributed, 0 unexplained") was computed by hand with a
# detector that could see neither a TRUNCATED fault message nor a MID-PRINT STOP.
# A desktop-exit wedge that hung the entire board reported *zero faults* under that
# detector, and it was live across the same window the figure covers. So the number
# was unsupported and nobody could re-derive it. Now anyone can.
#
# Definitions, kept identical to the original so the numbers stay comparable:
#   one log            = one test-cycle-* invocation = one power-on
#   success            = the `(psh)%` prompt appears
#   fault-bearing      = matches the fault-pattern set uart-summary.sh uses
# Added, because their absence is what made the old figure wrong:
#   wedge-suspect      = the log ends MID-PRINT (not on an interactive prompt),
#                        i.e. the target stopped while talking. This is the only
#                        signal that catches a board hang whose message never
#                        finished printing.
#
# Usage: ./scripts/reliability-tally.sh --since YYYYMMDD [log-dir]
#
# --since is REQUIRED and that is deliberate. Run over every log on disk the answer
# is meaningless: the archive reaches back to bring-up, when the board frequently
# did not boot at all and faults were being reproduced on purpose. Measured over
# everything it reports 88.2% and 50% fault-bearing, which says nothing about the
# current build. Always scope it to a window in which the build was expected to
# work.
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause
set -uo pipefail

since=""
dir="artifacts/rpi4b-uart"
while [ $# -gt 0 ]; do
    case "$1" in
        --since) since="${2:?--since needs YYYYMMDD}"; shift 2 ;;
        *) dir="$1"; shift ;;
    esac
done
[ -n "$since" ] || { echo "usage: $0 --since YYYYMMDD [log-dir]  (see the note in this file)" >&2; exit 2; }
[ -d "$dir" ] || { echo "no such directory: $dir" >&2; exit 2; }

# Same pattern set as scripts/uart-summary.sh, including the prefixes that make a
# truncated kernel message visible.
#
# 2026-09-10 addition -- `malloc: ` and `Double free detected`. The list above had
# NO pattern for the libphoenix allocator's corruption reports, so every
# heap-corruption occurrence in the archive was invisible to this detector: 31
# double-free reports across 23 logs (2026-06-28 -> 09-10) plus 18 corrupt-header
# / corrupt-neighbour / bad-bin-link reports, and all of them were counted as
# CLEAN. That is the same mistake as the truncated-message one above, in a new
# class: the figure said "0 fault-bearing" about runs that had detected heap
# corruption and _exit(EX_SOFTWARE)'d on it.
#
# Matched on the distinctive report TEXT, not on a `malloc:` prefix, for two
# measured reasons. A bare `malloc: ` is a substring of `test_malloc: Starting`,
# the banner six soak logs open with -- it turned the clean 1M-line mallocsoak run
# fault-bearing. And anchoring with \bmalloc: fixes that but then misses the
# report's own continuation lines, which carry a literal ANSI `[0m` immediately
# before `malloc:` so there is no word boundary there. The report wordings below
# have neither problem, and none of them collides with the USB driver's separate
# `usb_mem:` allocator tracer (`free-list head corrupt`, its own `caller=`).
#
# ⚠ And pick a fragment that CANNOT be split by an ANSI escape. The first version
# used `corrupt next neighbour`, which matched nothing: that report reaches the log
# as `malloc: corrupt ^[[0mnext^[[0m neighbour chunk header`, with escapes around a
# word in the MIDDLE of the phrase. Matching the tail `stopping coalesce` instead.
# Verified archive-wide: these patterns hit exactly the 3 known-positive logs
# (stk-flipdiag, stkguard1, stkship) out of 4679, and nothing else.
fault_re="Exception|Data Abort|panic|\bfault\b|ESR=|ELR=|FAR=|EC=|vm: page|corrupt process|LIB_ASSERT|assertion|double free\(\)|Double free detected|handed out twice|corrupt chunk header|stopping coalesce|not a plausible chunk"

total=0
prompt=0
faulted=0
wedge=0
declare -a wedge_logs=()
declare -a fault_logs=()

for f in "$dir"/rpi4b-uart-*.log; do
    [ -f "$f" ] || continue
    # Skip empty captures: they are a host-side artefact (a second concurrent cycle
    # on the shared UART), not a boot, and counting them either way is misleading.
    [ -s "$f" ] || continue

    # Window filter: the timestamp is in the filename (rpi4b-uart-YYYYMMDD-...).
    stamp=$(basename "$f" | sed -nE 's/^rpi4b-uart-([0-9]{8})-.*/\1/p')
    [ -n "$stamp" ] || continue
    [ "$stamp" -ge "$since" ] || continue
    total=$((total + 1))

    if grep -qa 'psh)%' "$f"; then
        prompt=$((prompt + 1))
    fi
    if grep -qaE "$fault_re" "$f"; then
        faulted=$((faulted + 1))
        fault_logs+=("$(basename "$f")")
    fi

    # Mid-print stop, with the same exemptions uart-summary.sh uses: a psh prompt
    # and Quake III's "]" console prompt legitimately end without a newline, and a
    # tail of pure ANSI escapes has no text in flight.
    if [ "$(tail -c 1 "$f" | wc -l)" -eq 0 ]; then
        partial=$(tail -n 1 "$f" | tr -d '\r\000' \
            | sed -e 's/\x1b\[[0-9;]*[a-zA-Z]//g' -e 's/\x1b//g' \
            | tr -s ' ' | sed -e 's/^ *//' -e 's/ *$//')
        case "$partial" in
            ''|*'(psh)%'|']') ;;   # ends ON a prompt (possibly after a timestamp prefix)
            *) wedge=$((wedge + 1)); wedge_logs+=("$(basename "$f"): ${partial:0:60}") ;;
        esac
    fi
done

if [ "$total" -eq 0 ]; then
    echo "no logs found in $dir" >&2
    exit 1
fi

pct=$(awk -v a="$prompt" -v b="$total" 'BEGIN{printf "%.1f", (a*100.0)/b}')
echo "=== boot reliability, recomputed from $dir since $since ==="
echo "  logs (non-empty)      : $total"
echo "  reached (psh)% prompt : $prompt  (${pct}%)"
echo "  fault-bearing logs    : $faulted"
echo "  wedge-suspect logs    : $wedge   (log ends mid-print — the class the old figure could not see)"
echo
if [ "${#wedge_logs[@]}" -gt 0 ]; then
    echo "  wedge-suspect detail:"
    for w in "${wedge_logs[@]}"; do echo "    $w"; done
    echo
fi
echo "NB a fault-bearing log is not automatically a system defect: many are a"
echo "deliberately reproduced bug, or an app-side abort (SuperTuxKart's intermittent"
echo "crash is the largest single contributor). Attribute before quoting."
