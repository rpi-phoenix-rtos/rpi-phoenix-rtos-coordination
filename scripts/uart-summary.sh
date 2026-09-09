#!/usr/bin/env bash
# Phoenix-RTOS Pi 4 UART log summarizer.
#
# Goal: replace ad-hoc grep/sed/wc combinations during boot diagnosis
# with one deterministic structured-output command. The output is
# bounded in size and free of shell-metacharacter constructs, so it
# can be added to .claude/settings.json as a single safe allowlist
# entry.
#
# Usage:
#   ./scripts/uart-summary.sh                 # most recent log
#   ./scripts/uart-summary.sh <label-substr>  # most recent log matching substring
#   ./scripts/uart-summary.sh /full/path.log  # explicit path
#
# Output sections (always in this order):
#   1. PATH      — log path + line count + on-disk size
#   2. STAGES    — yes/no for each canonical boot stage marker
#   3. CHILDREN  — list of "main: spawned X" + each child's own output lines
#   4. FAULTS    — count of fault-like patterns + (if any) the last 3
#   5. TIMING    — timestamps of first/last firmware line and Phoenix banner
#   6. TAIL      — last 8 lines
#
# Exit codes:
#   0 ok
#   1 invocation error
#   2 no log found

set -u
set -o pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
UART_DIR="${PHOENIX_UART_DIR:-${repo_root}/artifacts/rpi4b-uart}"

if [ ! -d "$UART_DIR" ]; then
    echo "uart-summary: UART log dir not found: $UART_DIR" >&2
    exit 1
fi

# Resolve target log.
target=""
if [ $# -ge 1 ] && [ -f "$1" ]; then
    target="$1"
elif [ $# -ge 1 ]; then
    # Match by label substring against filename.
    # Use shell globbing only (no -exec / find).
    pat="*$1*.log"
    # shellcheck disable=SC2012
    target="$(ls -t "$UART_DIR"/$pat 2>/dev/null | grep -v -- "-meta" | grep -v "\.meta\." | head -n 1 || true)"
else
    # Most recent .log file in the directory.
    # shellcheck disable=SC2012
    target="$(ls -t "$UART_DIR"/*.log 2>/dev/null | grep -v "\.meta\." | head -n 1 || true)"
fi

if [ -z "$target" ] || [ ! -f "$target" ]; then
    echo "uart-summary: no log file found" >&2
    exit 2
fi

# Section 1: path/size/lines.
lines=$(wc -l < "$target" | tr -d ' ')
size=$(wc -c < "$target" | tr -d ' ')
echo "=== PATH ==="
echo "log:   $target"
echo "lines: $lines"
echo "bytes: $size"

# Section 2: boot stages. Each line: STAGE marker_or_first_match.
echo
echo "=== STAGES ==="

check_stage() {
    local label="$1"
    local pattern="$2"
    if grep -qE "$pattern" "$target"; then
        first=$(grep -nE "$pattern" "$target" | head -n 1 | cut -d: -f1)
        echo "[YES] $label  (line $first)"
    else
        echo "[NO ] $label"
    fi
}

check_stage "firmware boot       " "arm_loader: Starting ARM"
check_stage "armstub markers     " "132AS0|^132"
check_stage "plo console_init    " "hal: console_init done"
check_stage "plo sctlr-M         " "mem: post-sctlr-M"
check_stage "plo hal_init done   " "hal: init complete"
check_stage "plo banner          " "Phoenix-RTOS loader"
check_stage "plo->kernel handoff " "hal: jump exit el1|hal: jump exit"
check_stage "kernel banner       " "Phoenix-RTOS microkernel"
check_stage "threads scheduler   " "threads: ready queued|threads: schedule"
check_stage "init thread spawn   " "main: spawn dummyfs-root"
check_stage "spawn loop done     " "main: spawn loop done|entering proc_reap"
check_stage "fbcon up            " "fbcon: ok"
check_stage "pcie running        " "pcie: enter main|pcie: linkUp"
check_stage "xhci running        " "xhci: capProbe|xhci: pre reset"
check_stage "psh tty open        " "psh: tty open|psh: ready"
check_stage "psh prompt          " "\\(psh\\)%"
# Network stages — matches test-cycle-netboot.sh's own boot-health probes
# and surfaces the late-boot subsystems the test-cycle banner doesn't.
check_stage "lwip started        " "lwip: genet|/sbin/lwip "
check_stage "genet link up       " "lwip: genet.*link up"
check_stage "netif has IP        " "static IP 10.42.0|dhcp_start: 0|netif waits for OFFER"

# Section 3: children.
echo
echo "=== CHILDREN ==="
# What did the init thread say it spawned? Lines look like:
#   <ESC>[0mmain: spawned dummyfs-root (2)
# strip the ANSI prefix and pull the child name.
spawned=$(grep "main: spawned " "$target" | sed -E 's/.*main: spawned ([A-Za-z0-9_-]+).*/\1/' | sort -u)
if [ -n "$spawned" ]; then
    while IFS= read -r child; do
        # Each child's own output lines start with "<child>:" (after optional ANSI reset).
        # Match the string "<child>:" anywhere except in "main: spawned <child>".
        own=$(grep -c "${child}:" "$target")
        # Subtract the "main: spawned X (N)" line which also contains "<child>:".
        # Actually those lines contain "spawned $child" but not "$child:" so no subtraction.
        echo "spawned: ${child}  (own_output_lines=${own})"
    done <<< "$spawned"
else
    echo "spawned: (none)"
fi

# Section 4: faults.
#
# The pattern list must tolerate a TRUNCATED message, which is why the kernel
# diagnostic prefixes below are matched on their first words rather than on
# "fault". Learned the hard way on 2026-09-09: a desktop-exit run that HUNG the
# board logged only "vm: page" -- the target died partway through printing
# "vm: page fault with a corrupt process pointer" -- so "\bfault\b" did not match
# and this section reported fault_pattern_matches: 0 on a wedged run. A detector
# that needs the target to finish its sentence is no good for exactly the faults
# that matter most.
fault_re="Exception|Data Abort|panic|\bfault\b|ESR=|ELR=|FAR=|EC=|vm: page|corrupt process|LIB_ASSERT|assertion"
echo
echo "=== FAULTS ==="
fault_count=$(grep -cE "$fault_re" "$target")
echo "fault_pattern_matches: $fault_count"
if [ "$fault_count" -gt 0 ]; then
    echo "--- last 3 ---"
    grep -nE "$fault_re" "$target" | tail -n 3
fi

# Section 4b: the silent wedge.
#
# Independent of any keyword: if the last byte of the log is not a newline, the
# target stopped in the MIDDLE of a print, which catches a wedge whose message we
# have never seen before.
#
# It is a HEURISTIC, not proof, and the exemptions below were each found by running
# it over all 92 of one day's logs rather than by reasoning:
#   * a psh PROMPT has no trailing newline, so a perfectly clean cycle ends mid-line
#     too -- the first version of this check flagged the clean desktop-exit run;
#   * Quake III's console prompt is a bare "]", same story, 4 more logs;
#   * a log whose tail is only ANSI escapes has no text in flight at all.
# What survives all three is a partial line with real TEXT in it, which is the
# signature worth looking at. Reported as SUSPECT, never as a verdict.
if [ -s "$target" ] && [ "$(tail -c 1 "$target" | wc -l)" -eq 0 ]; then
    partial=$(tail -n 1 "$target" | tr -d '\r' | sed -e 's/\x1b\[[0-9;]*[a-zA-Z]//g' -e 's/\x1b//g')
    partial_trimmed=$(printf '%s' "$partial" | tr -s ' ' | sed -e 's/^ *//' -e 's/ *$//')
    case "$partial_trimmed" in
        ''|'(psh)%'|'(psh)%'*'(psh)%'|']')
            echo "ends_mid_line: no (ends on an interactive prompt or bare escapes)"
            ;;
        *)
            echo "ends_mid_line: SUSPECT  <-- target may have stopped WHILE PRINTING (wedge/crash)"
            printf '    last partial line: %s\n' "$(printf '%s' "$partial_trimmed" | cut -c1-120)"
            ;;
    esac
else
    echo "ends_mid_line: no"
fi

# Section 5: timing.
echo
echo "=== TIMING ==="
fw_first=$(grep -nE "MESS:00:" "$target" | head -n 1 | cut -d: -f1-2 | head -c 80)
[ -n "$fw_first" ] && echo "fw_first: $fw_first" || echo "fw_first: (none)"
phx_banner_line=$(grep -nE "Phoenix-RTOS microkernel" "$target" | head -n 1 | cut -d: -f1)
[ -n "$phx_banner_line" ] && echo "phx_banner_line: $phx_banner_line" || echo "phx_banner_line: (none)"
proc_reap_line=$(grep -nE "entering proc_reap idle" "$target" | head -n 1 | cut -d: -f1)
[ -n "$proc_reap_line" ] && echo "proc_reap_line: $proc_reap_line" || echo "proc_reap_line: (none)"

# Section 6: tail.
echo
echo "=== TAIL (last 8 lines) ==="
tail -n 8 "$target"
