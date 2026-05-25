#!/usr/bin/env bash
#
# Software-trigger a Pi 4 reboot via the BCM2711 PM watchdog, by sending
# the 'r' command to the diag-udp responder running inside the Phoenix
# lwip-port (listening on UDP port 9999).
#
# Use this in place of `pi_power_off.sh && sleep 3 && pi_power_on.sh`
# when the Pi is alive and responding — software reboot is ~30 s faster
# and avoids the Meross smart-plug round-trip. Falls back is the caller's
# responsibility: if this script fails (Pi unresponsive), use the
# pi_power_off / pi_power_on pair as before.
#
# Usage:
#   ./scripts/pi_reboot.sh [--halt]
#
# With --halt the firmware comes up into halt mode instead of rebooting
# (stamps PM_RSTS with the 0x555 magic). The Pi will then need a
# power-cycle to come back; useful for clean shutdown only.
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
[ -f "$repo/.env.local" ] && . "$repo/.env.local"

target_ip="${PHX_PI_IP:-10.42.0.99}"
target_port=9999
cmd='r'
label='reboot'

if [ "${1:-}" = "--halt" ]; then
    cmd='h'
    label='halt'
fi

printf 'Sending %s to %s:%u\n' "$label" "$target_ip" "$target_port"

# `nc -u -w 1` sends + reads + closes within a 1 s window. Phoenix's
# diag-udp responder formats the reply, spawns the deferred-reboot
# thread, and returns within milliseconds — the bulk of the wait is
# the 100 ms in-Phoenix delay before the watchdog fires.
if ! resp=$(printf '%s' "$cmd" | timeout 3 nc -u -w 1 "$target_ip" "$target_port" 2>/dev/null); then
    printf 'ERROR: no response from %s:%u\n' "$target_ip" "$target_port" >&2
    printf 'Pi may be unreachable; fall back to pi_power_off.sh + pi_power_on.sh\n' >&2
    exit 1
fi

if [ -z "$resp" ]; then
    printf 'ERROR: empty reply from %s:%u\n' "$target_ip" "$target_port" >&2
    exit 1
fi

printf '%s\n' "$resp"

case "$label" in
    reboot)
        printf 'Pi will netboot again in ~60s.\n'
        ;;
    halt)
        printf 'Pi is halting; needs a power-cycle to resume.\n'
        ;;
esac
