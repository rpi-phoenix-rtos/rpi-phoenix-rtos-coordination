#!/usr/bin/env bash
# E2: give the netboot Pi outbound internet by NAT-ing its subnet out the host's
# internet NIC. Idempotent + additive (safe to re-run; does not touch netboot).
#
# Pair with the DHCP side (already baked into scripts/netboot-server.sh):
#   dhcp-option=3,<host_ip>  -> Pi's default gateway = this host
#   dhcp-option=6,8.8.8.8    -> Pi's DNS resolver (reached via this NAT)
# so the Pi auto-configures gateway+DNS from DHCP; this script just enables the
# host to forward+masquerade that traffic to the internet.
#
# HW-verified 2026-08-08: Phoenix Pi4 resolves example.com and completes an HTTP
# round-trip (DNS + routing + NAT). See project_pi4_internet_e2_feasibility.
#
# Host iptables rules are runtime (not persistent across host reboot) -> re-run this
# after a host reboot. Reverse with:  sudo iptables -t nat -D POSTROUTING -s <subnet>
#   -o <inet_nic> -j MASQUERADE ; sudo iptables -D FORWARD -s <subnet> -j ACCEPT ; ...
set -euo pipefail

PI_SUBNET="${PI_SUBNET:-10.42.0.0/24}"
INET_NIC="${INET_NIC:-$(ip route show default | awk '/default/ {print $5; exit}')}"

if [ -z "${INET_NIC}" ]; then
	echo "pi-internet-nat: could not detect the internet NIC (no default route)" >&2
	exit 1
fi

echo "pi-internet-nat: NAT ${PI_SUBNET} -> any uplink (default route now: ${INET_NIC})"

sudo sysctl -q -w net.ipv4.ip_forward=1

# Masquerade out of ANY interface, not just today's default-route NIC: the uplink can change
# while the server is up (a venue's Ethernet, or a phone tethered over USB plugged in later),
# and a rule pinned to one NIC then silently cuts the Pi off the internet.
sudo iptables -t nat -C POSTROUTING -s "${PI_SUBNET}" ! -d "${PI_SUBNET}" -j MASQUERADE 2>/dev/null \
	|| sudo iptables -t nat -A POSTROUTING -s "${PI_SUBNET}" ! -d "${PI_SUBNET}" -j MASQUERADE

sudo iptables -C FORWARD -s "${PI_SUBNET}" -j ACCEPT 2>/dev/null \
	|| sudo iptables -I FORWARD 1 -s "${PI_SUBNET}" -j ACCEPT

sudo iptables -C FORWARD -d "${PI_SUBNET}" -j ACCEPT 2>/dev/null \
	|| sudo iptables -I FORWARD 2 -d "${PI_SUBNET}" -j ACCEPT

echo "pi-internet-nat: done (idempotent). ip_forward=$(cat /proc/sys/net/ipv4/ip_forward)"
