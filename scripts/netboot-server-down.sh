#!/usr/bin/env bash
#
# Stop the in-VM dnsmasq started by scripts/netboot-server-up.sh.
# The bridged interface (lima1 inside the VM) is left configured.
#

set -euo pipefail

vm="${PHOENIX_VM:-phoenix-dev}"

if ! limactl list -f '{{.Name}} {{.Status}}' | grep -q "^${vm} Running$"; then
	printf 'VM "%s" not running; nothing to stop.\n' "$vm"
	exit 0
fi

limactl shell -y "$vm" -- /Users/witoldbolt/phoenix-rpi/scripts/vm-netboot-server.sh down
