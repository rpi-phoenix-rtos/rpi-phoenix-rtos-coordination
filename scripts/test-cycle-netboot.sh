#!/usr/bin/env bash
#
# One full automated test cycle for Pi 4 netboot:
#   1. ensure dnsmasq is running inside the phoenix-dev VM (no-op if so)
#   2. power off (clean slate)
#   3. power on; watch dnsmasq.log for Pi DHCP within $dhcp_wait_secs.
#      If silent, the en7 USB-C bridge into the VM is likely stale (this
#      happens whenever the laptop is undocked from a USB-C hub and the
#      adapter is replugged): run netboot-bridge-recover.sh (full VM
#      restart) and retry once.
#   4. capture UART for $capture_secs seconds
#   5. power off (always — guaranteed by EXIT trap)
#
# TFTP serves directly out of the buildroot bootfs tree from inside the
# VM — no copy step needed. Just rebuild then run this:
#
#   ./scripts/rebuild-rpi4b-fast.sh && ./scripts/test-cycle-netboot.sh --label probe
#

set -euo pipefail

repo="${PHOENIX_RPI_ROOT:-/Users/witoldbolt/phoenix-rpi}"
capture_secs="${RPI4B_NETBOOT_CAPTURE_SECS:-90}"
label=""
power_settle_secs="${RPI4B_POWER_SETTLE_SECS:-3}"
skip_server_up=0
dhcp_wait_secs="${RPI4B_DHCP_WAIT_SECS:-25}"
pi_mac="${RPI4B_PI_MAC:-dc:a6:32:3c:dd:f1}"
state_dir="${RPI4B_NETBOOT_STATE_DIR:-$repo/artifacts/netboot}"
dnsmasq_log="$state_dir/dnsmasq.log"
skip_bridge_recovery=0
uart_baud=""

usage() {
	cat <<EOF
Usage: test-cycle-netboot.sh [options]

  --label TEXT             short label appended to the UART log filename
  --capture-secs N         how long to capture UART (default $capture_secs)
  --skip-server-up         assume dnsmasq is already running in the VM
  --dhcp-wait-secs N       seconds to wait for Pi DHCP after power-on
                           (default $dhcp_wait_secs); 0 disables the watchdog
  --skip-bridge-recovery   on DHCP timeout, fail rather than restart the VM
  --baud N                 picocom baud (default 115200; try 103448 if
                           plo output is garbled — start4.elf reprograms
                           PL011 to 103448 right before kernel handoff)
  -h, --help               show this help

Bridge recovery: on DHCP timeout the script invokes
  scripts/netboot-bridge-recover.sh
which restarts the VM (re-creating the socket_vmnet bridge between en7
and lima1) and retries the boot once. After two failures it exits 1.
EOF
}

while [ $# -gt 0 ]; do
	case "$1" in
		--label)                 label="$2"; shift 2 ;;
		--capture-secs)          capture_secs="$2"; shift 2 ;;
		--skip-server-up)        skip_server_up=1; shift ;;
		--dhcp-wait-secs)        dhcp_wait_secs="$2"; shift 2 ;;
		--skip-bridge-recovery)  skip_bridge_recovery=1; shift ;;
		--baud)                  uart_baud="$2"; shift 2 ;;
		-h|--help)               usage; exit 0 ;;
		*) printf 'unknown arg: %s\n' "$1" >&2; usage >&2; exit 1 ;;
	esac
done

ts="$(date -u +%Y%m%d-%H%M%S)"
log_dir="$repo/artifacts/rpi4b-uart"
mkdir -p "$log_dir"
if [ -n "$label" ]; then
	log_path="$log_dir/rpi4b-uart-$ts-netboot-$label.log"
else
	log_path="$log_dir/rpi4b-uart-$ts-netboot.log"
fi

# Always power off on exit — Ctrl-C, error, normal completion, anything.
# Leaving the Pi powered on between cycles corrupts the next boot attempt
# (EEPROM/SD state, half-loaded firmware in DRAM, etc.), so this is the
# single most important invariant of the test cycle.
ensure_powered_off() {
	local rc=$?
	"$repo/scripts/pi_power_off.sh" >/dev/null 2>&1 || \
		printf 'WARNING: pi_power_off.sh failed on exit cleanup\n' >&2
	return "$rc"
}
trap ensure_powered_off EXIT INT TERM HUP

# 1. Server up.
if [ "$skip_server_up" = 0 ]; then
	"$repo/scripts/netboot-server-up.sh"
fi

# Wait up to $1 seconds for a fresh DHCPDISCOVER from the Pi to appear
# in dnsmasq.log past line $2. Returns 0 if seen, 1 on timeout. Used to
# detect a wedged en7 -> lima1 bridge (no packets crossing) without
# having to wait the full UART capture window to discover the failure.
wait_for_pi_dhcp() {
	local timeout="$1"
	local since_lines="$2"
	local started cur n new_lines
	started=$(date +%s)
	while :; do
		if [ -f "$dnsmasq_log" ]; then
			n=$(wc -l <"$dnsmasq_log" | awk '{print $1}')
			if [ "$n" -gt "$since_lines" ]; then
				new_lines=$(( n - since_lines ))
				if tail -n "$new_lines" "$dnsmasq_log" \
						| grep -qi "DHCPDISCOVER.*$pi_mac"; then
					return 0
				fi
			fi
		fi
		cur=$(date +%s)
		if [ $(( cur - started )) -ge "$timeout" ]; then
			return 1
		fi
		sleep 1
	done
}

snapshot_log_lines() {
	if [ -f "$dnsmasq_log" ]; then
		wc -l <"$dnsmasq_log" | awk '{print $1}'
	else
		printf '0'
	fi
}

# Watch dnsmasq.log for the Pi's DHCPDISCOVER, given the line-count
# snapshot taken just before the event we're waiting on. Returns 0 if
# DHCP seen, 1 on timeout.
watch_for_dhcp() {
	local timeout="$1"
	local pre_lines="$2"
	printf 'waiting up to %ss for Pi DHCP via bridge...\n' "$timeout"
	if wait_for_pi_dhcp "$timeout" "$pre_lines"; then
		printf 'Pi DHCP seen \xe2\x80\x94 bridge OK\n'
		return 0
	fi
	printf 'WARNING: no DHCP from Pi (%s) in %ss\n' "$pi_mac" "$timeout" >&2
	return 1
}

# 2-3. Power on the Pi and watchdog its DHCP. On timeout, recover the
# bridge by restarting the VM — but KEEP the Pi powered on across the
# recovery, so en7 stays "active" (link up) while socket_vmnet rebuilds
# its BPF capture. Powering the Pi off would make en7 go inactive, and a
# fresh socket_vmnet that comes up against an inactive en7 wedges the
# bridge again as soon as link returns.
"$repo/scripts/pi_power_off.sh" >/dev/null 2>&1 || true
sleep "$power_settle_secs"

pre_lines=$(snapshot_log_lines)
"$repo/scripts/pi_power_on.sh"

if [ "$dhcp_wait_secs" -gt 0 ]; then
	if ! watch_for_dhcp "$dhcp_wait_secs" "$pre_lines"; then
		if [ "$skip_bridge_recovery" = 1 ]; then
			printf 'ERROR: DHCP timeout, --skip-bridge-recovery set; aborting\n' >&2
			exit 1
		fi
		printf 'attempting bridge recovery (VM restart, Pi stays on)...\n' >&2
		"$repo/scripts/netboot-bridge-recover.sh"
		# After VM restart, dnsmasq.log was appended to by the new
		# instance — re-snapshot so we only count truly new lines.
		pre_lines=$(snapshot_log_lines)
		if ! watch_for_dhcp "$dhcp_wait_secs" "$pre_lines"; then
			printf 'ERROR: Pi still not DHCPing after VM restart; check\n' >&2
			printf '  - en7 USB-C ethernet adapter is plugged in\n' >&2
			printf '  - cable to the Pi is connected\n' >&2
			printf '  - Pi is being powered (try pi_power_on.sh manually)\n' >&2
			exit 1
		fi
	fi
fi

# 4. UART capture for the rest of the cycle.
exit_ms=$(( capture_secs * 1000 ))
printf 'capturing UART for %ss -> %s\n' "$capture_secs" "$log_path"
capture_args=( --log "$log_path" --exit-after "$exit_ms" )
if [ -n "$uart_baud" ]; then
	capture_args+=( --baud "$uart_baud" )
fi
"$repo/scripts/capture-rpi4b-uart.sh" "${capture_args[@]}" || true

# 5. Power off handled by EXIT trap.

if [ -s "$log_path" ]; then
	printf '\n=== test cycle complete ===\n'
	printf 'log:   %s\n' "$log_path"
	printf 'bytes: %s\n' "$(stat -f %z "$log_path")"
else
	printf '\n=== test cycle complete (log empty) ===\n'
	printf 'log:   %s\n' "$log_path"
	printf 'check that USB-UART is plugged and Pi was powered.\n' >&2
fi
