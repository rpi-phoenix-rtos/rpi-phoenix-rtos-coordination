#!/usr/bin/env bash
#
# Restart the Pi 4 netboot server fresh: stop dnsmasq, bounce the USB-Ethernet
# NIC (clear any stale link state and re-assert the netboot IP), then start
# dnsmasq again. Used by test-cycle-netboot.sh if the Pi's firmware DHCP does
# not appear within the watchdog window — the most common cause is the USB NIC
# needing a re-up after a replug, not anything in dnsmasq.
#
# (Formerly netboot-bridge-recover.sh — there is no VM or bridge on the Linux
# host, so the operation is simply a server + NIC restart.)
#

set -euo pipefail

repo="${PHOENIX_RPI_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}"

if [ -f "$repo/.env.local" ]; then
	set -a
	# shellcheck disable=SC1091
	. "$repo/.env.local"
	set +a
fi

iface="${RPI4B_NETBOOT_IFACE:-eth1}"
host_ip="${RPI4B_NETBOOT_HOST_IP:-10.42.0.1}"

printf '=== netboot server restart: bouncing dnsmasq + iface %s ===\n' "$iface"

"$repo/scripts/netboot-server-down.sh" 2>/dev/null || true

if ip link show "$iface" >/dev/null 2>&1; then
	# Best-effort: bounce the link to clear any stale USB-NIC state, and
	# ensure the netboot IP is present.
	sudo ip link set "$iface" down 2>/dev/null || true
	sudo ip link set "$iface" up 2>/dev/null || true
	cur=$(ip -4 -br addr show "$iface" 2>/dev/null | awk '{print $3}')
	if [ "${cur:-}" != "$host_ip/24" ]; then
		printf '=== reconfiguring %s -> %s/24 ===\n' "$iface" "$host_ip"
		sudo ip addr flush dev "$iface" 2>/dev/null || true
		sudo ip addr add "$host_ip/24" dev "$iface" 2>/dev/null || true
	fi
else
	printf 'WARNING: iface %s not present; USB-Ethernet cable unplugged?\n' "$iface" >&2
fi

"$repo/scripts/netboot-server-up.sh"
printf '=== netboot server restart complete ===\n'
