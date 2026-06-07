#!/usr/bin/env bash
#
# One-shot Pi 4 netboot cycle that waits for the psh prompt and sends
# a small command sequence to verify the shell is actually interactive.
#
#   1. ensure dnsmasq is running inside phoenix-dev VM
#   2. power off (clean slate)
#   3. power on
#   4. open the UART, wait for `psh: readcmd`, then send commands
#      via scripts/psh-interact.py — UART log written to artifacts/
#   5. power off (always — guaranteed by trap)
#
# Pairs with test-cycle-netboot.sh — same power & server flow, but
# replaces the timed UART capture with an interactive Python session.
#
# IMPORTANT: opening the serial device while a prior capture-rpi4b-uart
# process is still attached, OR re-running netboot-server-up while
# dnsmasq is alive, can wedge the en7->lima1 socket_vmnet bridge so
# subsequent boots get no DHCP. If that happens, run
# scripts/netboot-bridge-recover.sh while the Pi stays powered ON.
# Default baud is 115200 (matches test-cycle-netboot.sh firmware
# profile) — plo + kernel speak this rate end-to-end despite the
# firmware briefly reprogramming PL011 to 103448.

set -euo pipefail

repo="${PHOENIX_RPI_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}"
label="${LABEL:-psh-interact}"
power_settle_secs="${RPI4B_POWER_SETTLE_SECS:-3}"
skip_server_up=0
wait_secs="${PSH_WAIT_SECS:-150}"
idle_secs="${PSH_IDLE_SECS:-20}"
commands_default=( "help" "ps" "mem" "df" )
commands=()
uart_baud="115200"

usage() {
	cat <<EOF
Usage: test-cycle-psh-interact.sh [options] [-- command1 command2 ...]

  --label TEXT       short label appended to the log filename
  --wait-secs N      seconds to wait for psh prompt (default $wait_secs)
  --idle-secs N      seconds of UART idle after each command (default $idle_secs)
  --baud N           UART baud (default $uart_baud post-baud-switch)
  --skip-server-up   assume dnsmasq is already running in the VM
  -h, --help         show this help

If no commands are given on the command line, the default set is sent:
  ${commands_default[*]}
EOF
}

while [ $# -gt 0 ]; do
	case "$1" in
		--label)            label="$2"; shift 2 ;;
		--wait-secs)        wait_secs="$2"; shift 2 ;;
		--idle-secs)        idle_secs="$2"; shift 2 ;;
		--baud)             uart_baud="$2"; shift 2 ;;
		--skip-server-up)   skip_server_up=1; shift ;;
		--)                 shift; while [ $# -gt 0 ]; do commands+=("$1"); shift; done ;;
		-h|--help)          usage; exit 0 ;;
		*) printf 'unknown arg: %s\n' "$1" >&2; usage >&2; exit 1 ;;
	esac
done

if [ ${#commands[@]} -eq 0 ]; then
	commands=( "${commands_default[@]}" )
fi

ts="$(date +%Y%m%d-%H%M%S)"
log_path="$repo/artifacts/rpi4b-uart/rpi4b-uart-${ts}-${label}.log"

cleanup() {
	printf '\n[test-cycle-psh-interact] power off\n'
	"$repo/scripts/pi_power_off.sh" >/dev/null 2>&1 || true
}
trap cleanup EXIT INT TERM

if [ "$skip_server_up" = 0 ]; then
	"$repo/scripts/netboot-server-up.sh"
fi

printf '[test-cycle-psh-interact] power cycle\n'
"$repo/scripts/pi_power_off.sh" >/dev/null 2>&1 || true
sleep "$power_settle_secs"
"$repo/scripts/pi_power_on.sh"

printf '[test-cycle-psh-interact] running psh-interact.py\n'
printf '[test-cycle-psh-interact] log: %s\n' "$log_path"
printf '[test-cycle-psh-interact] commands: %s\n' "${commands[*]}"

# NOTE: do NOT `exec` here. exec replaces this shell with python, which discards
# the `trap cleanup EXIT` above, so the Pi would never get powered off when the
# interactive session ends (it was left running every time). Run python as a
# child and let the EXIT trap fire on return; preserve its exit status.
python3 "$repo/scripts/psh-interact.py" \
	--baud "$uart_baud" \
	--log "$log_path" \
	--wait-secs "$wait_secs" \
	--idle-secs "$idle_secs" \
	--commands "${commands[@]}"
exit $?
