#!/usr/bin/env bash
# Host-side air monitor for the Pi 4 WiFi data path.
#
# Two detectors, both L2/L3 (never a station byte counter -- that was the old
# "non-egress wall" measurement artifact):
#
#   1. tcpdump on the AP interface filtered by the Pi's MAC, so EVERY frame the
#      Pi transmits shows up regardless of protocol. The Pi's DHCP frames act as
#      the positive control: if they are absent, the detector is broken, not the
#      driver.
#   2. a tagged UDP sender: 1472-byte broadcasts to port 9997 whose payload byte
#      i is (i ^ 0x5A), which `wifi mtu` on the Pi verifies byte-for-byte.
#
# Usage: wifi-air-monitor.sh start [seconds] | stop | report
set -uo pipefail

IFACE="${WIFI_AP_IFACE:-wlp3s0}"
HOST_MAC="${WIFI_AP_MAC:-f4:28:9d:ce:07:9d}"
# Filter by EXCLUSION, not by the Pi's MAC: the firmware may rewrite the L2
# source address, so an `ether src <pi-mac>` filter can silently miss every
# frame the Pi sends (it did -- while dnsmasq was answering its DHCP, proving
# the frames arrived). Everything that is not the host's own traffic is a
# candidate, and the Pi's DHCP is the positive control.
FILTER="${WIFI_AIR_FILTER:-not ether src $HOST_MAC}"
OUT_DIR="${WIFI_AIR_OUT:-artifacts/wifi-air}"
CAP="$OUT_DIR/tx-capture.txt"
SND="$OUT_DIR/rx-sender.log"

cd "$(dirname "$0")/.." || exit 2
mkdir -p "$OUT_DIR"

case "${1:-start}" in
start)
	secs="${2:-460}"
	# pkill -x (exact name), never pkill -f: a -f pattern also matches this
	# script's own argv and kills the caller.
	sudo -n pkill -x tcpdump 2>/dev/null
	pkill -x -f "$OUT_DIR/rx-sender.py" 2>/dev/null
	pkill -x -f "$OUT_DIR/tx-listener.py" 2>/dev/null
	: > "$CAP"
	sudo -n nohup timeout "$secs" tcpdump -i "$IFACE" -nne -s0 "$FILTER" >"$CAP" 2>&1 &
	cat > "$OUT_DIR/rx-sender.py" <<'PY'
import socket, sys, time
payload = bytes((i ^ 0x5A) & 0xFF for i in range(1472))
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
end = time.time() + float(sys.argv[2])
n = 0
while time.time() < end:
    try:
        s.sendto(payload, (sys.argv[1], 9997)); n += 1
    except OSError as e:
        print("send error", e, flush=True)
    time.sleep(0.2)
print("sent", n, "tagged 1472B probes", flush=True)
PY
	nohup python3 "$OUT_DIR/rx-sender.py" 10.43.0.255 "$secs" >"$SND" 2>&1 &

	# TX detector: a UDP socket on 9998. tcpdump's tap on this AP interface does
	# NOT see frames from associated stations (it misses even the Pi's DHCP,
	# which dnsmasq demonstrably answers), so a socket -- the same path dnsmasq
	# uses -- is the only detector that can be trusted here.
	cat > "$OUT_DIR/tx-listener.py" <<'PY2'
import socket, sys, time
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("0.0.0.0", 9998))
s.settimeout(1.0)
end = time.time() + float(sys.argv[1])
seen = 0
while time.time() < end:
    try:
        data, addr = s.recvfrom(4096)
    except socket.timeout:
        continue
    seen += 1
    ok = all(b == ((i ^ 0x5A) & 0xFF) for i, b in enumerate(data))
    print("RX-FROM-PI from=%s len=%d pattern=%s" % (addr[0], len(data), "OK" if ok else "MISMATCH"), flush=True)
print("listener saw", seen, "datagrams from the Pi", flush=True)
PY2
	nohup python3 "$OUT_DIR/tx-listener.py" "$secs" >"$OUT_DIR/tx-listener.log" 2>&1 &
	sleep 2
	echo "wifi-air-monitor: capturing $IFACE for ${secs}s [$FILTER] -> $CAP"
	echo "wifi-air-monitor: tagged RX probes -> 10.43.0.255:9997 (log $SND)"
	;;
stop)
	sudo -n pkill -x tcpdump 2>/dev/null
	pkill -x -f "$OUT_DIR/rx-sender.py" 2>/dev/null
	echo "wifi-air-monitor: stopped"
	;;
report)
	echo "=== UDP :9998 listener (the trustworthy TX detector) ==="
	cat "$OUT_DIR/tx-listener.log" 2>/dev/null | head -8
	# grep -c prints "0" AND exits 1 on no match, so `|| echo 0` would make this
	# two lines and break the arithmetic below. Assign, then default.
	total=$(grep -c "^[0-9][0-9]:" "$CAP" 2>/dev/null) || total="${total:-0}"
	echo "=== frames on the air that are not the host's own: $total"
	if [ "$total" = "0" ]; then
		echo "    NOTE: 0 frames means the DETECTOR failed (the Pi's DHCP frames"
		echo "    would appear here too) -- do not read this as a driver verdict."
	fi
	echo "--- DHCP (positive control):"
	grep -E "\.(67|68):|bootp|Request from" "$CAP" 2>/dev/null | head -4
	echo "--- data frames to port 9998 (the MTU test's TX):"
	grep -E "\.9998:" "$CAP" 2>/dev/null | head -8
	echo "--- largest frame lengths seen:"
	grep -oE "length [0-9]+" "$CAP" 2>/dev/null | sort -t' ' -k2 -rn | head -4
	;;
*)
	echo "usage: $0 start [seconds] | stop | report" >&2
	exit 2
	;;
esac
