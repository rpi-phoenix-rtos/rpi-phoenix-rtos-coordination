#!/usr/bin/env bash
#
# Bring up the Pi 4 netboot server. Architecture:
#
#   Pi 4 RJ45 ----- en7 (host USB-C) ===bridged via socket_vmnet===> lima1 (VM)
#                                                                   |
#                                                                   v
#                                                            dnsmasq + TFTP
#                                                            (10.42.0.1/24)
#
# The host does no packet handling at all; en7 is just a passive bridge
# member. dnsmasq runs inside the phoenix-dev Lima VM, served straight
# from the buildroot bootfs tree (no host-side mirror).
#
# Sibling scripts:
#   scripts/netboot-server-down.sh stop the in-VM dnsmasq
#   scripts/test-cycle-netboot.sh  one full power-cycle + UART capture
#   scripts/vm-netboot-server.sh   the in-guest worker (do not call directly)
#

set -euo pipefail

vm="${PHOENIX_VM:-phoenix-dev}"

if ! command -v limactl >/dev/null 2>&1; then
	printf 'limactl not found on PATH\n' >&2
	exit 1
fi

if ! limactl list -f '{{.Name}} {{.Status}}' | grep -q "^${vm} Running$"; then
	printf 'VM "%s" is not running. Start it with: limactl start %s\n' "$vm" "$vm" >&2
	exit 1
fi

# Pass through any RPI4B_NETBOOT_* overrides so callers can tweak the
# subnet without editing files.
env_args=()
while IFS='=' read -r k v; do
	case "$k" in
		RPI4B_NETBOOT_*|PHOENIX_BUILDROOT) env_args+=("$k=$v") ;;
	esac
done < <(env)

limactl shell -y "$vm" -- env ${env_args[@]+"${env_args[@]}"} \
	/Users/witoldbolt/phoenix-rpi/scripts/vm-netboot-server.sh up
