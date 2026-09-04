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
stamp=0
wait_secs="${PSH_WAIT_SECS:-150}"
idle_secs="${PSH_IDLE_SECS:-20}"
inter_cmd_secs="${PSH_INTER_CMD_SECS:-3}"
max_cmd_secs="${PSH_MAX_CMD_SECS:-120}"
commands_default=( "help" "ps" "mem" "df" )
commands=()
uart_baud="115200"

# HDMI grabber (Linux host, optional) — mirrors test-cycle-netboot.sh so a
# scripted-psh cycle can also capture what's on-screen (e.g. an X server painting
# the framebuffer). Skips cleanly if the grabber device is absent.
hdmi_grabber="${RPI4B_HDMI_GRABBER:-/dev/video4}"
hdmi_interval="${RPI4B_HDMI_INTERVAL:-15}"
# Once the run reaches a known-ready marker in the UART log (a game's first
# presented frame, say), snapshot DENSELY: the fixed 15 s cadence is what made
# visual checks flaky -- on a slow start every tick landed during loading and the
# grader had no frame to judge, which reads as a failure of the thing under test.
hdmi_dense_on="${RPI4B_HDMI_DENSE_ON:-}"
hdmi_dense_interval="${RPI4B_HDMI_DENSE_INTERVAL:-5}"
ready_line="${PSH_READY_LINE:-}"
ready_extra_secs="${PSH_READY_EXTRA_SECS:-60}"
hdmi_dir="${RPI4B_HDMI_DIR:-$repo/artifacts/hdmi}"
hdmi_pid=""

usage() {
	cat <<EOF
Usage: test-cycle-psh-interact.sh [options] [-- command1 command2 ...]

  --label TEXT       short label appended to the log filename
  --wait-secs N      seconds to wait for psh prompt (default $wait_secs)
  --ready-line ERE   regex marking the command ready (e.g. 'present [0-9]+'); until it
                     matches, --max-cmd-secs is only the deadline for REACHING it
  --ready-extra-secs N  capture this long after --ready-line matches (default $ready_extra_secs)
  --hdmi-dense-on ERE   once this matches the UART log, snapshot every
                     \$RPI4B_HDMI_DENSE_INTERVAL s (default $hdmi_dense_interval) instead of $hdmi_interval s
  --idle-secs N      seconds of UART idle after each command (default $idle_secs)
  --inter-cmd-secs N seconds to wait between commands (default $inter_cmd_secs;
                     raise it so a post-takeover retry lands after NFS root mounts)
  --baud N           UART baud (default $uart_baud post-baud-switch)
  --skip-server-up   assume dnsmasq is already running in the VM
  --cmd-file FILE    read commands from FILE (one per line; appended to any -- commands)
  -h, --help         show this help

If no commands are given on the command line, the default set is sent:
  ${commands_default[*]}
EOF
}

while [ $# -gt 0 ]; do
	case "$1" in
		--label)            label="$2"; shift 2 ;;
		--wait-secs)        wait_secs="$2"; shift 2 ;;
		--ready-line)       ready_line="$2"; shift 2 ;;
		--ready-extra-secs) ready_extra_secs="$2"; shift 2 ;;
		--hdmi-dense-on)    hdmi_dense_on="$2"; shift 2 ;;
		--idle-secs)        idle_secs="$2"; shift 2 ;;
		--inter-cmd-secs)   inter_cmd_secs="$2"; shift 2 ;;
		--max-cmd-secs)     max_cmd_secs="$2"; shift 2 ;;
		--baud)             uart_baud="$2"; shift 2 ;;
		--skip-server-up)   skip_server_up=1; shift ;;
		--cmd-file)         mapfile -t _cf < "$2"; commands+=("${_cf[@]}"); shift 2 ;;
		--stamp)            stamp=1; shift ;;
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

hdmi_label_base() {
	local ts2
	ts2="$(date -u +%Y%m%d-%H%M%S)"
	if [ -n "$label" ]; then printf '%s/%s-%s' "$hdmi_dir" "$ts2" "$label"; else printf '%s/%s' "$hdmi_dir" "$ts2"; fi
}
hdmi_grab_one() {
	local out="$1"
	[ -e "$hdmi_grabber" ] || return 1
	timeout --foreground 5 ffmpeg -y -loglevel error -f v4l2 -i "$hdmi_grabber" \
		-frames:v 1 "$out" </dev/null >/dev/null 2>&1 || return 1
}
start_hdmi_periodic() {
	[ "$hdmi_interval" -gt 0 ] || return 0
	[ -e "$hdmi_grabber" ] || { printf 'HDMI: grabber %s absent, skipping snapshots\n' "$hdmi_grabber" >&2; return 0; }
	mkdir -p "$hdmi_dir"
	( interval="$hdmi_interval"
	  while sleep "$interval"; do
		hdmi_grab_one "$(hdmi_label_base)-tick.png" || true
		# Switch to the dense cadence the first time the marker shows up. grep -c,
		# not -q: -q exits on the first match, and a SIGPIPE'd producer in a
		# pipeline has bitten this repo before.
		if [ -n "$hdmi_dense_on" ] && [ "$interval" != "$hdmi_dense_interval" ] &&
			[ "$(grep -acE "$hdmi_dense_on" "$log_path" 2>/dev/null || echo 0)" -gt 0 ]; then
			interval="$hdmi_dense_interval"
			printf 'HDMI: marker matched, snapshotting every %ss\n' "$interval" >&2
		fi
	  done ) &
	hdmi_pid=$!
}
stop_hdmi_periodic() {
	if [ -n "$hdmi_pid" ] && kill -0 "$hdmi_pid" 2>/dev/null; then
		kill "$hdmi_pid" 2>/dev/null || true
		wait "$hdmi_pid" 2>/dev/null || true
	fi
	hdmi_pid=""
}

cleanup() {
	stop_hdmi_periodic
	if [ -e "$hdmi_grabber" ]; then mkdir -p "$hdmi_dir"; hdmi_grab_one "$(hdmi_label_base)-final.png" || true; fi
	printf '\n[test-cycle-psh-interact] power off\n'
	"$repo/scripts/pi_power_off.sh" >/dev/null 2>&1 || true
}
trap cleanup EXIT INT TERM

if [ "$skip_server_up" = 0 ]; then
	"$repo/scripts/netboot-server-up.sh"

	# Refuse a netboot cycle when the TFTP boot blob is the SD variant.
	#
	# Building `--variant sd` (which the flashable image needs) overwrites
	# loader.disk in the very tree TFTP serves. Boot that with no card in the Pi
	# and sdstorage_srv spends 50 tries looking for /dev/mmcblk0p2, never mounts a
	# root, and no user-space program runs -- so the cycle "succeeds", captures a
	# screenful of nothing, and the failure looks like whatever you were testing.
	# It has cost this project two rounds of wasted Pi cycles and one bogus
	# regression hunt, so make it impossible rather than remembering it.
	#
	# Fix when it fires: ./scripts/rebuild-rpi4b-fast.sh --scope project \
	#   --variant nfsroot --with-showcase --with-ports --with-tests
	_ld="$repo/.buildroot/_boot/${RPI4B_TARGET:-aarch64a72-generic-rpi4b}/rpi4b-bootfs/loader.disk"
	if [ -f "$_ld" ] && strings -a "$_ld" 2>/dev/null | grep -q 'mmcblk0p2'; then
		printf '\n[test-cycle-psh-interact] REFUSING: the TFTP loader.disk is the SD-BOOT\n' >&2
		printf '  variant (it mounts /dev/mmcblk0p2). A netboot cycle with no card in the\n' >&2
		printf '  Pi will not reach user space, and every result would be a false negative.\n' >&2
		printf '  Rebuild the nfsroot boot blob first:\n' >&2
		printf '    ./scripts/rebuild-rpi4b-fast.sh --scope project --variant nfsroot \\\n' >&2
		printf '      --with-showcase --with-ports --with-tests\n' >&2
		printf '  (Or pass --skip-server-up if you really are SD-booting with a card in.)\n' >&2
		exit 3
	fi
fi

printf '[test-cycle-psh-interact] power cycle\n'
"$repo/scripts/pi_power_off.sh" >/dev/null 2>&1 || true
sleep "$power_settle_secs"
"$repo/scripts/pi_power_on.sh"
start_hdmi_periodic

printf '[test-cycle-psh-interact] running psh-interact.py\n'
printf '[test-cycle-psh-interact] log: %s\n' "$log_path"
printf '[test-cycle-psh-interact] commands: %s\n' "${commands[*]}"

# NOTE: do NOT `exec` here. exec replaces this shell with python, which discards
# the `trap cleanup EXIT` above, so the Pi would never get powered off when the
# interactive session ends (it was left running every time). Run python as a
# child and let the EXIT trap fire on return; preserve its exit status.
ready_args=()
if [ -n "$ready_line" ]; then
	ready_args=( --ready-line "$ready_line" --ready-extra-secs "$ready_extra_secs" )
fi

python3 "$repo/scripts/psh-interact.py" \
	--baud "$uart_baud" \
	--log "$log_path" \
	--wait-secs "$wait_secs" \
	--idle-secs "$idle_secs" \
	--inter-cmd-secs "$inter_cmd_secs" \
	--max-cmd-secs "$max_cmd_secs" \
	"${ready_args[@]}" \
	$( [ "$stamp" = 1 ] && printf -- '--stamp' ) \
	--commands "${commands[@]}"
exit $?
