#!/usr/bin/env bash
#
# Autonomous USB-keyboard counter probe (#127). Netboot the Pi, then query the
# diag-udp 'k' keyboard counters (insertions/opens/reports) at several points
# as USB enumerates (which is slow, ~minutes), then power off.
#
# No keypress needed for the first two counters:
#   insertions>0 -> keyboard enumerated, /dev/kbd0 created
#   opens>0      -> something opened /dev/kbd0, so interrupt URBs are submitted
#                   (polling is live) — pl011_kbdthr is the opener
#   reports>0    -> HID reports actually arrived (needs a human keypress)
#
# Replies are saved under artifacts/diag-udp/ by diag-udp-probe.sh. The SD card
# must be OUT of the Pi (netboot). Usage: diag-kbd-probe.sh
#
set -uo pipefail

repo="${PHOENIX_RPI_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}"

"$repo/scripts/netboot-server-up.sh" >/dev/null 2>&1 || true
"$repo/scripts/pi_power_on.sh" || true

# Probe at ~200/320/440/560 s of uptime to settle slow-vs-failing enumeration:
# the earlier run showed insertions=0 through 300 s, so push the window out.
i=0
up=0
for t in 200 140 140; do
	sleep "$t"
	up=$((up + t))
	i=$((i + 1))
	printf '=== diag-kbd-probe: query %d (≈%ss uptime) ===\n' "$i" "$up"
	"$repo/scripts/diag-udp-probe.sh" k "kbd-counter-$i" 20 4 || true
done

"$repo/scripts/pi_power_off.sh" >/dev/null 2>&1 || true
printf '=== diag-kbd-probe: done; replies under artifacts/diag-udp/ ===\n'
