#!/usr/bin/env bash
#
# get-pi-ip.sh — print the Pi 4's current DHCP-leased IP from dnsmasq.
#
# Reads artifacts/netboot/dnsmasq.leases, finds the line whose MAC
# matches RPI4B_MAC (default dc:a6:32:3c:dd:f1), and prints the IP.
#
# Useful for follow-on tooling (e.g. diag-udp-probe.sh) that needs the
# Pi's current address. When Phoenix's lwip uses the static-IP fallback
# (10.42.0.99), the dnsmasq lease still reflects the netboot-firmware-
# stage DHCP (a different IP). To probe a running Phoenix that has
# replaced the static IP via dhcp_start, this script gives you the
# DHCP-assigned address.
#
# Exit codes:
#   0  printed an IPv4 address
#   1  could not read leases file or MAC not found
#
# Usage:
#   ./scripts/get-pi-ip.sh
#   RPI4B_MAC=01:23:45:67:89:ab ./scripts/get-pi-ip.sh
set -u

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mac="${RPI4B_MAC:-dc:a6:32:3c:dd:f1}"
leases="${RPI4B_LEASES:-${repo}/artifacts/netboot/dnsmasq.leases}"

if [ ! -r "$leases" ]; then
    printf 'get-pi-ip: cannot read %s\n' "$leases" >&2
    exit 1
fi

# Lease line format: <expiry_unix> <mac> <ip> <hostname-or-*> <client-id-or-*>
# MAC is case-insensitive; lowercase normalizes both sides.
mac_lc=$(printf '%s' "$mac" | tr 'A-Z' 'a-z')
ip=$(grep -i " ${mac_lc} " "$leases" | tail -n 1 | awk '{print $3}')

if [ -z "$ip" ]; then
    printf 'get-pi-ip: no lease for MAC %s in %s\n' "$mac" "$leases" >&2
    exit 1
fi

printf '%s\n' "$ip"
