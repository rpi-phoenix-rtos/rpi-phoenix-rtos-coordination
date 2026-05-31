#!/usr/bin/env bash
#
# Stop the Pi 4 netboot dnsmasq on this Linux host. The actual stop logic lives
# in scripts/netboot-server.sh; this wrapper sets the Linux env and dispatches.
# Bring the server down before an SD-card boot test so the firmware can't
# netboot and falls back to the SD card.
#

set -euo pipefail

repo="${PHOENIX_RPI_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}"

if [ -f "$repo/.env.local" ]; then
	set -a
	# shellcheck disable=SC1091
	. "$repo/.env.local"
	set +a
fi

export PHOENIX_BUILDROOT="${PHOENIX_BUILDROOT:-$repo/.buildroot}"
export RPI4B_NETBOOT_STATE_DIR="${RPI4B_NETBOOT_STATE_DIR:-$repo/artifacts/netboot}"

exec "$repo/scripts/netboot-server.sh" down
