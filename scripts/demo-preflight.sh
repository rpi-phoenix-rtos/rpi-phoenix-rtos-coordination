#!/usr/bin/env bash
#
# demo-preflight.sh — check, in one command, everything the Pi needs from this host before a
# netboot demo: the boot image, the netboot DHCP/TFTP server, the NFS root, the NAT to the
# internet through whatever uplink the host has right now (Ethernet, a phone tethered over
# USB, WiFi), and DNS/HTTPS from the host. Read-only (iptables is listed with `sudo -n`).
#
#   ./scripts/netboot-server-up.sh      # once after the host boots (sync + NAT + dnsmasq)
#   ./scripts/demo-preflight.sh         # then this; every line must say OK
#
# Prints one "PREFLIGHT <check> OK|FAIL <detail>" line per check and a summary; exit 1 on a FAIL.
# The fix for each FAIL is in its detail text.

set -u
repo="$(cd "$(dirname "$0")/.." && pwd)"
export_root="${PHOENIX_NFS_EXPORT:-/srv/phoenix-rpi4-nfs-gcc16}"
fails=0

ok()   { printf 'PREFLIGHT %-12s OK   %s\n' "$1" "$2"; }
fail() { printf 'PREFLIGHT %-12s FAIL %s\n' "$1" "$2"; fails=$((fails + 1)); }

# 1. the boot image: a netboot (NFS-root) loader in the TFTP root
blob="$("${repo}/scripts/check-netboot-blob.sh" 2>&1)"
if grep -q 'rootfs: *nfsroot' <<<"${blob}"; then
	ok boot-image "$(grep -o 'built: .*' <<<"${blob}")"
else
	fail boot-image "not an NFS-root loader (an SD build overwrote it?): rebuild without --variant sd"
fi

# 2. the netboot DHCP/TFTP server
if pgrep -f "dnsmasq -C ${repo}/artifacts/netboot/dnsmasq.conf" >/dev/null; then
	ok dnsmasq "netboot DHCP/TFTP is running"
else
	fail dnsmasq "not running: ./scripts/netboot-server-up.sh"
fi

# 3. the netboot NIC holds 10.42.0.1
if ip -4 addr show | grep -q 'inet 10\.42\.0\.1/'; then
	ok netboot-nic "$(ip -4 -o addr show | awk '/10\.42\.0\.1\// {print $2}')"
else
	fail netboot-nic "no interface has 10.42.0.1: check the built-in Ethernet (enp1s0f0) cable to the switch and RPI4B_NETBOOT_IFACE in .env.local (docs/LAB-SETUP.md), then ./scripts/netboot-server-up.sh"
fi

# 4. the NFS root
if [ "$(systemctl is-active nfs-server 2>/dev/null)" = active ] && [ -x "${export_root}/bin/psh" ]; then
	ok nfs "${export_root} exported, nfs-server active"
else
	fail nfs "nfs-server not active or ${export_root} empty: sudo systemctl restart nfs-server"
fi
if [ -e "${export_root}/etc/build-versions" ]; then
	ok build-ver "$(grep -m1 -o 'phoenix-rtos-kernel *[0-9a-f]*' "${export_root}/etc/build-versions")"
fi

# 5. the uplink and the NAT for the Pi's subnet (any uplink; scripts/pi-internet-nat.sh)
up="$(ip route show default | awk '/default/ {print $5; exit}')"
if [ -n "${up}" ]; then
	ok uplink "default route via ${up}"
else
	fail uplink "no default route: connect Ethernet or tether the phone over USB"
fi
if [ "$(cat /proc/sys/net/ipv4/ip_forward)" = 1 ] &&
		sudo -n iptables -t nat -S POSTROUTING 2>/dev/null | grep -q -- '-s 10.42.0.0/24 ! -d 10.42.0.0/24 -j MASQUERADE'; then
	ok nat "10.42.0.0/24 masqueraded out of any uplink, forwarding on"
else
	fail nat "missing: ./scripts/pi-internet-nat.sh (needs sudo)"
fi

# 6. the internet from the host (what the Pi will get through the NAT; the Pi's DNS is 8.8.8.8)
if timeout 5 ping -c1 -W3 8.8.8.8 >/dev/null 2>&1; then
	ok internet "8.8.8.8 answers"
else
	fail internet "8.8.8.8 does not answer: the uplink has no internet (or blocks ICMP; see https below)"
fi
if timeout 8 nslookup en.wikipedia.org 8.8.8.8 >/dev/null 2>&1; then
	ok dns "8.8.8.8 resolves (the Pi uses it)"
else
	fail dns "8.8.8.8 does not resolve: the uplink blocks public DNS; the Pi's web pages will fail"
fi
if timeout 10 curl -s -o /dev/null -w '%{http_code}' https://en.wikipedia.org/ 2>/dev/null | grep -q '^[23]'; then
	ok https "https://en.wikipedia.org reachable"
else
	fail https "HTTPS from the host fails: a captive portal or no internet"
fi

# 7. the soft-AP the smart plug joins (and the Pi's WiFi, when used): NetworkManager's
#    shared-mode profile `phoenix-ap` on the host's WiFi (2.4 GHz: the Meross MSS620 has no
#    5 GHz), NATed by NetworkManager out of whatever uplink holds the default route
if [ "$(nmcli -t -f GENERAL.STATE connection show phoenix-ap 2>/dev/null | cut -d: -f2)" = activated ]; then
	ok soft-ap "phoenix-ap up: SSID $(nmcli -t -f 802-11-wireless.ssid connection show phoenix-ap | cut -d: -f2), 10.43.0.1, band $(nmcli -t -f 802-11-wireless.band connection show phoenix-ap | cut -d: -f2)"
else
	fail soft-ap "phoenix-ap is down: sudo nmcli connection up phoenix-ap (the smart plug reaches its cloud through it)"
fi

# 8. the smart plug that powers the Pi (Meross cloud; it must be online through the soft-AP)
plug="${MEROSS_PLUG_SCRIPT:-/home/houp/meross-plug/plug.py}"
if [ -x "${plug}" ]; then
	st="$(timeout 60 "${plug}" status 2>&1 | tail -1)"
	case "${st}" in
		*": ON"|*": OFF") ok plug "${st}" ;;
		*) fail plug "the plug does not answer (${st:-no reply}): is it paired and on PhoenixNet? See docs/LAB-SETUP.md" ;;
	esac
else
	ok plug "no plug helper (${plug}); power the Pi by hand"
fi

# 9. the serial console is free (a stray capture holds it)
if command -v fuser >/dev/null && fuser /dev/ttyUSB0 >/dev/null 2>&1; then
	fail uart "/dev/ttyUSB0 is held by another program (a running capture?)"
else
	ok uart "/dev/ttyUSB0 free"
fi

echo "PREFLIGHT summary fails=${fails}"
[ "${fails}" = 0 ]
