#!/usr/bin/env bash
#
# Bring the Pi 4 netboot bridge back to life after a USB-Ethernet
# unplug/replug event. Behaviour depends on host OS:
#
#   Darwin  — the test rig used a Lima VM (phoenix-dev) that bridged the
#             host's en7 USB-C ethernet into the VM via socket_vmnet.
#             socket_vmnet's BPF capture wedges on link-state churn, and
#             the only reliable cure is a full VM restart.
#
#   Linux   — dnsmasq runs directly on the host's dedicated USB-Ethernet
#             NIC; there is no VM bridge to rebuild. Bounce the dnsmasq
#             instance and re-up the interface in case a replug brought
#             it down.
#
# Side effects:
#   - macOS: stops VM, restarts VM, restarts dnsmasq. Pi must stay
#     powered on across the recovery to keep en7's link "active".
#   - Linux: re-ups the interface (RPI4B_NETBOOT_IFACE / .env.local)
#     and bounces dnsmasq.
#

set -euo pipefail

repo="${PHOENIX_RPI_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}"
host_os="$(uname -s)"

if [ -f "$repo/.env.local" ]; then
	set -a
	# shellcheck disable=SC1091
	. "$repo/.env.local"
	set +a
fi

case "$host_os" in
	Darwin)
		vm="${PHOENIX_VM:-phoenix-dev}"
		if ! command -v limactl >/dev/null 2>&1; then
			printf 'limactl not on PATH; cannot recover bridge.\n' >&2
			exit 1
		fi
		printf '=== bridge recovery: stopping netboot server ===\n'
		"$repo/scripts/netboot-server-down.sh" 2>/dev/null || true

		printf '=== bridge recovery: stopping VM %s ===\n' "$vm"
		limactl stop "$vm"

		printf '=== bridge recovery: starting VM %s ===\n' "$vm"
		limactl start "$vm"

		printf '=== bridge recovery: starting netboot server ===\n'
		"$repo/scripts/netboot-server-up.sh"

		printf '=== bridge recovery complete ===\n'
		;;
	Linux)
		iface="${RPI4B_NETBOOT_IFACE:-eth1}"
		host_ip="${RPI4B_NETBOOT_HOST_IP:-10.42.0.1}"
		printf '=== bridge recovery (Linux): no VM to restart ===\n'
		printf '=== bouncing dnsmasq + iface %s ===\n' "$iface"

		"$repo/scripts/netboot-server-down.sh" 2>/dev/null || true

		if ip link show "$iface" >/dev/null 2>&1; then
			# Best-effort: bring link down/up to clear any USB-NIC
			# state, and ensure the netboot IP is present.
			sudo ip link set "$iface" down 2>/dev/null || true
			sudo ip link set "$iface" up 2>/dev/null || true
			cur=$(ip -4 -br addr show "$iface" 2>/dev/null | awk '{print $3}')
			if [ "${cur:-}" != "$host_ip/24" ]; then
				printf '=== reconfiguring %s -> %s/24 ===\n' "$iface" "$host_ip"
				sudo ip addr flush dev "$iface" 2>/dev/null || true
				sudo ip addr add "$host_ip/24" dev "$iface" 2>/dev/null || true
			fi
		else
			printf 'WARNING: iface %s not present; cable unplugged?\n' "$iface" >&2
		fi

		"$repo/scripts/netboot-server-up.sh"
		printf '=== bridge recovery complete ===\n'
		;;
	*)
		printf 'unsupported host OS: %s\n' "$host_os" >&2
		exit 1
		;;
esac
