#!/usr/bin/env bash
# Serve NTP from this host to the Pi 4 lab networks.
#
# The Pi 4 has no battery-backed RTC, so without a time source its clock is
# boot-relative: time() starts near 0, every file timestamp it writes over NFS is
# wrong (measured: a file stamped 99 against the server's real epoch, which is
# what libc/misc stat_nlink_size_blk_tim was failing on), and TLS validity checks
# have nothing sane to compare against.
#
# Idempotent: writes a chrony drop-in only if it is missing or has changed, and
# only then restarts chrony. Safe to call from other scripts.
#
# On the Pi, once the network is up:   ntpclient -s 10.42.0.1
set -uo pipefail

CONF=/etc/chrony/conf.d/phoenix-lab.conf
NETBOOT_NET="${RPI4B_NETBOOT_NET:-10.42.0.0/24}"
WIFI_NET="${RPI4B_WIFI_NET:-10.43.0.0/24}"

want=$(cat <<CONF_EOF
# Managed by scripts/lab-timeserver-up.sh -- do not edit by hand.
# Serve time to the Pi 4 lab networks (netboot and the WiFi AP).
allow ${NETBOOT_NET}
allow ${WIFI_NET}
# Serve even when this host has no upstream sync, so a lab with no internet
# still gets a coherent (if unauthoritative) clock rather than none.
local stratum 10
CONF_EOF
)

if [ "$(cat "$CONF" 2>/dev/null)" = "$want" ]; then
	echo "lab-timeserver: chrony already serving ${NETBOOT_NET} + ${WIFI_NET}"
else
	printf '%s\n' "$want" | sudo -n tee "$CONF" >/dev/null || {
		echo "lab-timeserver: could not write $CONF (needs sudo)" >&2
		exit 1
	}
	sudo -n systemctl restart chrony 2>/dev/null || sudo -n systemctl restart chronyd || {
		echo "lab-timeserver: chrony restart failed" >&2
		exit 1
	}
	echo "lab-timeserver: chrony now serving ${NETBOOT_NET} + ${WIFI_NET}"
fi

if ss -lun 2>/dev/null | grep -qE ':123\b'; then
	echo "lab-timeserver: UDP/123 listener up"
else
	echo "lab-timeserver: WARNING no UDP/123 listener -- the Pi cannot sync" >&2
fi
