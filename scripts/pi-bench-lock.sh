#!/usr/bin/env bash
#
# pi-bench-lock.sh — run a command while holding the Pi bench lock.
#
#   ./scripts/pi-bench-lock.sh ./scripts/test-cycle-netboot.sh --capture-secs 180 --label x
#
# The Pi 4 bench (UART /dev/ttyUSB0, the Meross plug, the netboot dnsmasq) is shared with
# another of the owner's projects (2026-10-17). Both sides take /home/houp/.pi-bench.lock (flock)
# for the whole of any Pi power/UART cycle and for any change to the netboot server, so two cycles
# never collide. Waits for the lock (PI_BENCH_LOCK_WAIT seconds, default 3600) and fails if it
# cannot get it.

set -euo pipefail
lock="${PI_BENCH_LOCK:-/home/houp/.pi-bench.lock}"
wait_s="${PI_BENCH_LOCK_WAIT:-3600}"
[ "$#" -gt 0 ] || { echo "usage: $0 <command> [args...]" >&2; exit 2; }
exec 9>"${lock}"
if ! flock -w "${wait_s}" 9; then
	echo "pi-bench-lock: could not get ${lock} within ${wait_s} s (another session's Pi cycle?)" >&2
	exit 75
fi
echo "pi-bench-lock: holding ${lock} for: $*" >&2
"$@"
