#!/usr/bin/env bash
#
# radio-ap-up.sh — stand up a WPA2 WiFi access point on the host's wireless
# interface so the Phoenix-RTOS Raspberry Pi 4 can JOIN it (radio-as-transport,
# owner directive 2026-08-12 #4). The Pi's BCM43455 already scans; this gives it
# a known WPA2-PSK AP to associate with, get DHCP, and use as a faster (and
# wire-free) alternative to the 100 Mbps netboot ethernet.
#
# SAFETY: the AP lives on a SEPARATE wireless interface (default wlp3s0) and a
# SEPARATE subnet (10.43.0.0/24) from the wired netboot NIC (enx.../10.42.0.1),
# so netboot/NFS is never touched. Uses NetworkManager AP mode (no hostapd
# install); ipv4.method=shared gives DHCP + NAT to the host uplink for free.
#
# Usage:  sudo ./scripts/radio-ap-up.sh          (tear down: radio-ap-down.sh)
# Override via env: RADIO_AP_IFACE / RADIO_AP_SSID / RADIO_AP_PSK / RADIO_AP_CHAN
set -euo pipefail

IFACE="${RADIO_AP_IFACE:-wlp3s0}"
SSID="${RADIO_AP_SSID:-PhoenixNet}"
# The PSK lives only in the NFS export's /etc/wifi.conf (never in the repo or the rootfs-overlay).
PSK="${RADIO_AP_PSK:-$(sed -n 's/^psk=//p' "${RADIO_AP_CONF:-/srv/phoenix-rpi4-nfs-gcc16/etc/wifi.conf}" 2>/dev/null || true)}"
[ -n "$PSK" ] || { echo "radio-ap: no PSK (set RADIO_AP_PSK or RADIO_AP_CONF=<wifi.conf>)" >&2; exit 1; }
CHAN="${RADIO_AP_CHAN:-6}"
CON="phoenix-ap"
GW="10.43.0.1/24"

echo "radio-ap: (re)creating AP connection '$CON' on $IFACE (SSID=$SSID ch$CHAN WPA2, gw=${GW%/*})"
nmcli connection delete "$CON" >/dev/null 2>&1 || true
# autoconnect: the Pi's WiFi join (and every gate boot) expects PhoenixNet; it must survive a
# host reboot (2026-10-06: it did not, and every boot logged a failed join)
nmcli connection add type wifi ifname "$IFACE" con-name "$CON" autoconnect yes ssid "$SSID"
nmcli connection modify "$CON" \
	802-11-wireless.mode ap \
	802-11-wireless.band bg \
	802-11-wireless.channel "$CHAN" \
	wifi-sec.key-mgmt wpa-psk \
	wifi-sec.psk "$PSK" \
	ipv4.method shared \
	ipv4.addresses "$GW"
nmcli connection up "$CON"

sleep 3
echo "=== AP interface state ==="
iw dev "$IFACE" info 2>/dev/null | grep -E 'Interface|type|channel|ssid|txpower' || true
ip -o -4 addr show "$IFACE" || true
echo "=== AP dnsmasq (NM shared) ==="
ps -eo pid,args 2>/dev/null | grep -E "dnsmasq.*$IFACE|NetworkManager.*dnsmasq" | grep -v grep | head || true
echo "radio-ap: UP — SSID='$SSID' WPA2 ch$CHAN on $IFACE @ ${GW%/*}, PSK=<hidden> (from ${RADIO_AP_CONF:-the export /etc/wifi.conf})"
echo "radio-ap: netboot NIC untouched (verify: ip addr show enx* still 10.42.0.1)"
