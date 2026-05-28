#!/usr/bin/env bash
#
# diag-udp-probe.sh — send a single diag-udp command to the Pi's lwip
# UDP responder (port 9999) and capture the reply to artifacts/diag-udp/.
#
# The Pi must already be booted and network-reachable; run this while a
# `test-cycle-netboot.sh` keeps the Pi powered (its capture-secs window),
# or any time the Pi is up. Most diag commands are one-shot per power
# cycle (they re-POR the WiFi chip at entry), so this sends EXACTLY once.
#
# Usage:
#   diag-udp-probe.sh <cmd-char> <label> [ready_wait_s] [send_timeout_s] [ip] [port]
#
# Examples:
#   ./scripts/diag-udp-probe.sh G wifi-pmu-fix 120 15
#   ./scripts/diag-udp-probe.sh X usb-xhci-bringup 120 8
#
set -u

CMD="${1:?usage: diag-udp-probe.sh <cmd-char> <label> [ready_wait_s] [send_timeout_s] [ip] [port]}"
LABEL="${2:?label required}"
READY_WAIT="${3:-120}"
SEND_TMO="${4:-10}"
IP="${5:-}"
PORT="${6:-9999}"

REPO="$(cd "$(dirname "$0")/.." && pwd)"

# If no IP was supplied OR the special value "auto" was supplied, ask the
# dnsmasq lease file for the Pi's current DHCP-assigned address. The
# static-IP fallback (10.42.0.99) is unchanged; it remains the easiest
# default for the static-IP genet path. Auto-discovery is needed only
# when Phoenix's dhcp_start has cleared the static IP — see TD-Eth-DHCP
# investigation note in drivers/bcm-genet.c.
if [ -z "$IP" ] || [ "$IP" = "auto" ]; then
    IP="$("${REPO}/scripts/get-pi-ip.sh" 2>/dev/null || echo 10.42.0.99)"
fi
OUTDIR="${REPO}/artifacts/diag-udp"
mkdir -p "${OUTDIR}"
OUT="${OUTDIR}/$(date +%Y-%m-%d-%H%M%S)-${LABEL}.txt"

{
  echo "=== diag-udp-probe cmd='${CMD}' label='${LABEL}' ${IP}:${PORT} $(date -u +%H:%M:%S) ==="
  echo "=== waiting up to ${READY_WAIT}s for ${IP} to answer ICMP ==="
} | tee "${OUT}"

ready=0
for ((i = 0; i < READY_WAIT; i++)); do
  if ping -c1 -W1 "${IP}" >/dev/null 2>&1; then
    ready=1
    echo "host up at +${i}s" | tee -a "${OUT}"
    break
  fi
  sleep 1
done

if [ "${ready}" -ne 1 ]; then
  echo "ERROR: ${IP} not reachable within ${READY_WAIT}s (Pi not booted / responder down)" | tee -a "${OUT}"
  exit 1
fi

# Small settle so the lwip UDP responder thread is past init once ICMP answers.
sleep 3

echo "=== sending '${CMD}' (recv window ${SEND_TMO}s) ===" | tee -a "${OUT}"
printf '%s' "${CMD}" | nc -u -w "${SEND_TMO}" "${IP}" "${PORT}" | tee -a "${OUT}"
echo "" | tee -a "${OUT}"
echo "=== saved: ${OUT} ==="
