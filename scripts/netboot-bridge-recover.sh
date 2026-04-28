#!/usr/bin/env bash
#
# Bring the netboot lima1 bridge back to life when the host's en7 USB-C
# ethernet was unplugged/replugged (e.g. when undocking the laptop from
# a USB-C hub). socket_vmnet's BPF capture on en7 goes stale across that
# event: en7 shows "active" again on the host and lima1 stays "UP" in the
# VM, but no frames cross the bridge. Restarting the VM fully tears down
# and re-establishes the socket_vmnet bridge.
#
# Usage: netboot-bridge-recover.sh
#
# Side effects: stops the in-VM dnsmasq server, stops the VM, starts the
# VM, restarts dnsmasq. The Pi is NOT touched — and the caller MUST keep
# the Pi powered on across the recovery so en7 stays "active" (link up)
# while the VM and socket_vmnet come up. A fresh socket_vmnet that finds
# en7 inactive at start time still wedges the bridge after link returns.
#

set -euo pipefail

repo="${PHOENIX_RPI_ROOT:-/Users/witoldbolt/phoenix-rpi}"
vm="${PHOENIX_VM:-phoenix-dev}"

printf '=== bridge recovery: stopping netboot server ===\n'
"$repo/scripts/netboot-server-down.sh" 2>/dev/null || true

printf '=== bridge recovery: stopping VM %s ===\n' "$vm"
limactl stop "$vm"

printf '=== bridge recovery: starting VM %s ===\n' "$vm"
limactl start "$vm"

printf '=== bridge recovery: starting netboot server ===\n'
"$repo/scripts/netboot-server-up.sh"

printf '=== bridge recovery complete ===\n'
