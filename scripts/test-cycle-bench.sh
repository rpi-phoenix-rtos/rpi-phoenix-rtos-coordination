#!/usr/bin/env bash
#
# test-cycle-bench.sh — run N back-to-back netboot cycles and summarize
# the boot-stage pass rate. Useful for measuring flakiness in subsystems
# that are known to be silicon- or boot-timing-sensitive (VL805 USB,
# BCM43455 WiFi).
#
# Usage:
#   ./scripts/test-cycle-bench.sh <N> <label> [--capture-secs <s>]
#
# Examples:
#   ./scripts/test-cycle-bench.sh 5 vl805-baseline
#   ./scripts/test-cycle-bench.sh 10 dhcp-trial --capture-secs 240
#
# Each trial is labeled "<label>-T<i>" and logged under
# artifacts/rpi4b-uart/. After all trials run, uart-summary.sh is
# called on each log and pass/fail counts are aggregated across the
# canonical STAGES (psh prompt, lwip started, etc.).
#
# Exits 0 if all trials complete (regardless of subsystem outcome).

set -u
set -o pipefail

if [ $# -lt 2 ]; then
    echo "usage: test-cycle-bench.sh <N> <label> [--capture-secs <s>]" >&2
    exit 1
fi

N="$1"
label="$2"
shift 2

capture_secs_arg=()
if [ $# -ge 2 ] && [ "$1" = "--capture-secs" ]; then
    capture_secs_arg=( --capture-secs "$2" )
    shift 2
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"

if ! [[ "$N" =~ ^[1-9][0-9]*$ ]]; then
    echo "test-cycle-bench: N must be a positive integer, got '$N'" >&2
    exit 1
fi

printf '=== bench start: %d trials, label="%s" ===\n' "$N" "$label"

logs=()
for i in $(seq 1 "$N"); do
    trial_label="${label}-T${i}"
    printf '\n=== trial %d/%d (%s) ===\n' "$i" "$N" "$trial_label"
    "${repo_root}/scripts/test-cycle-netboot.sh" --label "$trial_label" "${capture_secs_arg[@]}" || true
    log=$(ls -t "${repo_root}/artifacts/rpi4b-uart"/rpi4b-uart-*-"$trial_label".log 2>/dev/null | head -n 1 || true)
    if [ -n "$log" ]; then
        logs+=( "$log" )
    fi
done

printf '\n\n=== BENCH SUMMARY: label="%s" trials=%d logs=%d ===\n' "$label" "$N" "${#logs[@]}"

if [ "${#logs[@]}" -eq 0 ]; then
    printf 'no logs produced — bench infrastructure broken\n' >&2
    exit 1
fi

# Per-stage pass rate across all logs.
declare -A pass

# Stages to track — match check_stage labels in uart-summary.sh.
stages=(
    "kernel banner"
    "fbcon up"
    "psh prompt"
    "lwip started"
    "genet link up"
    "netif has IP"
)

for log in "${logs[@]}"; do
    out=$("${repo_root}/scripts/uart-summary.sh" "$log" 2>/dev/null || true)
    for stage in "${stages[@]}"; do
        if printf '%s\n' "$out" | grep -q "\[YES\] ${stage}"; then
            pass["$stage"]=$(( ${pass["$stage"]:-0} + 1 ))
        fi
    done
done

printf '%-22s %s\n' "STAGE" "PASS / $N"
printf '%-22s %s\n' "----------------------" "-------"
for stage in "${stages[@]}"; do
    n=${pass["$stage"]:-0}
    printf '  %-20s %d\n' "$stage" "$n"
done

echo
echo "logs:"
for log in "${logs[@]}"; do
    printf '  %s\n' "$log"
done
