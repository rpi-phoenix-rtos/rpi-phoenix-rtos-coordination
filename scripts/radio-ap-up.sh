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
# Override via env: RADIO_AP_IFACE / RADIO_AP_SSID / RADIO_AP_PSK / RADIO_AP_CHAN /
#   RADIO_AP_BAND (bg | a) / RADIO_AP_WIDTH (auto | 20mhz | 40mhz | 80mhz)
# Throughput (KNOWN-ISSUES F1): the default bg/ch6/auto is HT20, 72 Mbit/s at best for the Pi's 1x1
# BCM43455. The host mt7925e can run the AP at 5 GHz VHT80 (iw list: Band 2, VHT80 SGI; regdomain PL
# allows 5150-5250 @ 80 MHz with no DFS/no-IR, i.e. ch36-48):
#   RADIO_AP_BAND=a RADIO_AP_CHAN=36 RADIO_AP_WIDTH=80mhz sudo -E ./scripts/radio-ap-up.sh
# Verify the width took: `iw dev wlp3s0 info` (width: 80 MHz) and `wifi stats` on the Pi (radio ... bw=80).
# 2.4 GHz 40 MHz (RADIO_AP_BAND=bg RADIO_AP_WIDTH=40mhz) may be refused or narrowed by 20/40 coexistence
# with the neighbouring networks on ch 4/8; check `iw dev` before believing a result.
set -euo pipefail

IFACE="${RADIO_AP_IFACE:-wlp3s0}"
SSID="${RADIO_AP_SSID:-PhoenixNet}"
# The PSK lives only in the NFS export's /etc/wifi.conf (never in the repo or the rootfs-overlay).
# /etc/wifi.conf.off: the same file while the Pi's boot-time join is switched off (owner 2026-10-06:
# not testing WiFi now; rename it back to wifi.conf to make the Pi join at boot again).
CONF="${RADIO_AP_CONF:-/srv/phoenix-rpi4-nfs-gcc16/etc/wifi.conf}"
[ -f "$CONF" ] || CONF="${CONF}.off"
PSK="${RADIO_AP_PSK:-$(sed -n 's/^psk=//p' "$CONF" 2>/dev/null || true)}"
[ -n "$PSK" ] || { echo "radio-ap: no PSK (set RADIO_AP_PSK or RADIO_AP_CONF=<wifi.conf>)" >&2; exit 1; }
CHAN="${RADIO_AP_CHAN:-6}"
BAND="${RADIO_AP_BAND:-bg}"
WIDTH="${RADIO_AP_WIDTH:-auto}"
CON="phoenix-ap"
GW="10.43.0.1/24"

echo "radio-ap: (re)creating AP connection '$CON' on $IFACE (SSID=$SSID band=$BAND ch$CHAN width=$WIDTH WPA2, gw=${GW%/*})"
nmcli connection delete "$CON" >/dev/null 2>&1 || true
# autoconnect: the Pi's WiFi join (and every gate boot) expects PhoenixNet; it must survive a
# host reboot (2026-10-06: it did not, and every boot logged a failed join)
nmcli connection add type wifi ifname "$IFACE" con-name "$CON" autoconnect yes ssid "$SSID"
nmcli connection modify "$CON" \
	802-11-wireless.mode ap \
	802-11-wireless.band "$BAND" \
	802-11-wireless.channel "$CHAN" \
	802-11-wireless.channel-width "$WIDTH" \
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
echo "radio-ap: UP — SSID='$SSID' WPA2 band=$BAND ch$CHAN width=$WIDTH on $IFACE @ ${GW%/*}, PSK=<hidden> (from ${RADIO_AP_CONF:-the export /etc/wifi.conf})"
echo "radio-ap: netboot NIC untouched (verify: ip addr show enx* still 10.42.0.1)"
