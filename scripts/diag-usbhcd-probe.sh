#!/usr/bin/env bash
#
# Autonomous xHCI bring-up outcome probe (#129). Netboot the Pi, wait for the
# embedded usb/xhci bring-up to settle, then read the diag-udp 'U' counters over
# the network — bypassing the back-pressured UART where the bring-up debug()
# output drains slowly and char-interleaved.
#
# 'U' reports (set by sources/phoenix-rtos-devices/usb/xhci/xhci.c diag globals):
#   eventsSeen>0 -> controller's inbound DMA writes land: the @idx -1 "wall" fell
#   fix19Rc=0    -> FIX-19 (RC_BAR2 re-settle) armed (lastCtx set; premise holds)
#                   non-zero/negative (e.g. -ENODEV) -> inert -> test meaningless
#   bringupRc=0  -> xhci_init succeeded (controller usable)
# Also reads 'k' (kbd enumeration: insertions>0). The SD card must be OUT.
#
# Each boot is one trial (per-boot non-determinism is documented). Usage:
#   diag-usbhcd-probe.sh [N_BOOTS] [SETTLE_SECS]
# N_BOOTS default 1, SETTLE_SECS default 210. Two boots (~9 min) fit one Bash
# timeout; run the script repeatedly for more trials.
set -uo pipefail

repo="${PHOENIX_RPI_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}"
n_boots="${1:-1}"
settle="${2:-210}"

"$repo/scripts/netboot-server-up.sh" >/dev/null 2>&1 || true

b=0
while [ "$b" -lt "$n_boots" ]; do
	b=$((b + 1))
	printf '=== diag-usbhcd-probe: BOOT %d/%d (settle %ss) ===\n' "$b" "$n_boots" "$settle"
	"$repo/scripts/pi_power_on.sh" || true
	sleep "$settle"
	# 'D' (devnodes): stat /dev/kbd0 etc. after settle — a capture-timing-
	# independent "did USB enumerate the keyboard end to end?" check, valid now
	# that USB is a standalone process (#129; 'U'/'k' read embedded HCD state
	# that no longer exists). present=1 on /dev/kbd0 == full success this boot.
	"$repo/scripts/diag-udp-probe.sh" D "usbhcd-devnode-boot-$b" 25 4 || true
	"$repo/scripts/pi_power_off.sh" >/dev/null 2>&1 || true
	# Brief cooldown between boots so the VL805/bridge fully power-cycles.
	if [ "$b" -lt "$n_boots" ]; then
		sleep 8
	fi
done

printf '=== diag-usbhcd-probe: done; replies under artifacts/diag-udp/ ===\n'
