#!/usr/bin/env bash
#
# One full automated test cycle for Pi 4 netboot:
#   1. ensure dnsmasq is running inside the phoenix-dev VM (no-op if so)
#   2. power off (clean slate)
#   3. power on; watch dnsmasq.log for Pi DHCP within $dhcp_wait_secs.
#      If silent, the en7 USB-C bridge into the VM is likely stale (this
#      happens whenever the laptop is undocked from a USB-C hub and the
#      adapter is replugged): run netboot-bridge-recover.sh (full VM
#      restart) and retry once.
#   4. capture UART for $capture_secs seconds
#   5. power off (always — guaranteed by EXIT trap)
#
# TFTP serves directly out of the buildroot bootfs tree from inside the
# VM — no copy step needed. Just rebuild then run this:
#
#   ./scripts/rebuild-rpi4b-fast.sh && ./scripts/test-cycle-netboot.sh --label probe
#

set -euo pipefail

repo="${PHOENIX_RPI_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}"

if [ -f "$repo/.env.local" ]; then
	set -a
	# shellcheck disable=SC1091
	. "$repo/.env.local"
	set +a
fi

capture_secs="${RPI4B_NETBOOT_CAPTURE_SECS:-360}"
label=""
power_settle_secs="${RPI4B_POWER_SETTLE_SECS:-3}"
skip_server_up=0
dhcp_wait_secs="${RPI4B_DHCP_WAIT_SECS:-25}"
pi_mac="${RPI4B_PI_MAC:-dc:a6:32:3c:dd:f1}"
state_dir="${RPI4B_NETBOOT_STATE_DIR:-$repo/artifacts/netboot}"
dnsmasq_log="$state_dir/dnsmasq.log"
skip_bridge_recovery=0
uart_baud=""

# HDMI grabber options (Linux host only).
#  - RPI4B_HDMI_GRABBER:    /dev/videoN of the USB grabber (default video4
#                           on this lab — video0..3 are the laptop webcam).
#  - RPI4B_HDMI_INTERVAL:   seconds between periodic grabs while Pi is on
#                           (default 25s). Set to 0 to disable periodic
#                           capture; the start/end snapshots still run.
#  - RPI4B_HDMI_DIR:        where to write *.png (default artifacts/hdmi/).
hdmi_grabber="${RPI4B_HDMI_GRABBER:-/dev/video4}"
hdmi_interval="${RPI4B_HDMI_INTERVAL:-25}"
hdmi_dir="${RPI4B_HDMI_DIR:-$repo/artifacts/hdmi}"
hdmi_pid=""

usage() {
	cat <<EOF
Usage: test-cycle-netboot.sh [options]

  --label TEXT             short label appended to the UART log filename
  --capture-secs N         how long to capture UART (default $capture_secs)
  --skip-server-up         assume dnsmasq is already running in the VM
  --dhcp-wait-secs N       seconds to wait for Pi DHCP after power-on
                           (default $dhcp_wait_secs); 0 disables the watchdog
  --skip-bridge-recovery   on DHCP timeout, fail rather than restart the VM
  --baud N                 picocom baud (default 115200; try 103448 if
                           plo output is garbled — start4.elf reprograms
                           PL011 to 103448 right before kernel handoff)
  -h, --help               show this help

Bridge recovery: on DHCP timeout the script invokes
  scripts/netboot-bridge-recover.sh
which restarts the VM (re-creating the socket_vmnet bridge between en7
and lima1) and retries the boot once. After two failures it exits 1.
EOF
}

while [ $# -gt 0 ]; do
	case "$1" in
		--label)                 label="$2"; shift 2 ;;
		--capture-secs)          capture_secs="$2"; shift 2 ;;
		--skip-server-up)        skip_server_up=1; shift ;;
		--dhcp-wait-secs)        dhcp_wait_secs="$2"; shift 2 ;;
		--skip-bridge-recovery)  skip_bridge_recovery=1; shift ;;
		--baud)                  uart_baud="$2"; shift 2 ;;
		-h|--help)               usage; exit 0 ;;
		*) printf 'unknown arg: %s\n' "$1" >&2; usage >&2; exit 1 ;;
	esac
done

ts="$(date -u +%Y%m%d-%H%M%S)"
log_dir="$repo/artifacts/rpi4b-uart"
mkdir -p "$log_dir"
if [ -n "$label" ]; then
	log_path="$log_dir/rpi4b-uart-$ts-netboot-$label.log"
else
	log_path="$log_dir/rpi4b-uart-$ts-netboot.log"
fi

# HDMI screenshot helpers. The lab has a USB HDMI grabber that shows the
# Pi 4's framebuffer output (post `fbcon: ok`). Snapshots taken during the
# cycle let us see what's on-screen at each moment without needing to be
# physically present, and they cover the gap between `fbcon: ok` on UART
# and the post-`fbcon: ok` kernel klog drain to `pl011_thr` (which is slow
# and serialised behind the pcie/xhci scan, so the HDMI view is often the
# first signal that something happened).
hdmi_label_base() {
	local ts
	ts="$(date -u +%Y%m%d-%H%M%S)"
	if [ -n "$label" ]; then
		printf '%s/%s-%s' "$hdmi_dir" "$ts" "$label"
	else
		printf '%s/%s' "$hdmi_dir" "$ts"
	fi
}

hdmi_grab_one() {
	local out="$1"
	[ -e "$hdmi_grabber" ] || return 1
	# Wrap ffmpeg in a 5 s timeout: if another process holds /dev/video4
	# (e.g. a user-side HDMI preview), the v4l2 open can block indefinitely,
	# stalling the test cycle. 5 s is plenty for a single-frame capture
	# when the device is free; on contention we skip the snapshot and
	# proceed — the UART log is the source of truth, HDMI is a bonus.
	timeout --foreground 5 ffmpeg -y -loglevel error -f v4l2 -i "$hdmi_grabber" \
		-frames:v 1 "$out" </dev/null >/dev/null 2>&1 || return 1
}

start_hdmi_periodic() {
	local interval="$1"
	[ "$interval" -gt 0 ] || return 0
	[ -e "$hdmi_grabber" ] || { printf 'HDMI: grabber %s not present, skipping periodic snapshots\n' "$hdmi_grabber" >&2; return 0; }
	mkdir -p "$hdmi_dir"
	(
		# Run until killed by the cycle's EXIT trap; quietly absorb errors.
		while sleep "$interval"; do
			hdmi_grab_one "$(hdmi_label_base)-tick.png" || true
		done
	) &
	hdmi_pid=$!
}

stop_hdmi_periodic() {
	if [ -n "$hdmi_pid" ] && kill -0 "$hdmi_pid" 2>/dev/null; then
		kill "$hdmi_pid" 2>/dev/null || true
		wait "$hdmi_pid" 2>/dev/null || true
	fi
	hdmi_pid=""
}

# Always power off on exit — Ctrl-C, error, normal completion, anything.
# Leaving the Pi powered on between cycles corrupts the next boot attempt
# (EEPROM/SD state, half-loaded firmware in DRAM, etc.), so this is the
# single most important invariant of the test cycle. Grab a final HDMI
# snapshot first (Pi still on) so we see end-state before power-off.
ensure_powered_off() {
	local rc=$?
	stop_hdmi_periodic
	if [ -e "$hdmi_grabber" ]; then
		mkdir -p "$hdmi_dir"
		hdmi_grab_one "$(hdmi_label_base)-final.png" || true
	fi
	"$repo/scripts/pi_power_off.sh" >/dev/null 2>&1 || \
		printf 'WARNING: pi_power_off.sh failed on exit cleanup\n' >&2
	return "$rc"
}
trap ensure_powered_off EXIT INT TERM HUP

# 1. Server up.
if [ "$skip_server_up" = 0 ]; then
	"$repo/scripts/netboot-server-up.sh"
fi

# Wait up to $1 seconds for a fresh DHCPDISCOVER from the Pi to appear
# in dnsmasq.log past line $2. Returns 0 if seen, 1 on timeout. Used to
# detect a wedged en7 -> lima1 bridge (no packets crossing) without
# having to wait the full UART capture window to discover the failure.
wait_for_pi_dhcp() {
	local timeout="$1"
	local since_lines="$2"
	local started cur n new_lines
	started=$(date +%s)
	while :; do
		if [ -f "$dnsmasq_log" ]; then
			n=$(wc -l <"$dnsmasq_log" | awk '{print $1}')
			if [ "$n" -gt "$since_lines" ]; then
				new_lines=$(( n - since_lines ))
				if tail -n "$new_lines" "$dnsmasq_log" \
						| grep -qi "DHCPDISCOVER.*$pi_mac"; then
					return 0
				fi
			fi
		fi
		cur=$(date +%s)
		if [ $(( cur - started )) -ge "$timeout" ]; then
			return 1
		fi
		sleep 1
	done
}

snapshot_log_lines() {
	if [ -f "$dnsmasq_log" ]; then
		wc -l <"$dnsmasq_log" | awk '{print $1}'
	else
		printf '0'
	fi
}

# Watch dnsmasq.log for the Pi's DHCPDISCOVER, given the line-count
# snapshot taken just before the event we're waiting on. Returns 0 if
# DHCP seen, 1 on timeout.
watch_for_dhcp() {
	local timeout="$1"
	local pre_lines="$2"
	printf 'waiting up to %ss for Pi DHCP via bridge...\n' "$timeout"
	if wait_for_pi_dhcp "$timeout" "$pre_lines"; then
		printf 'Pi DHCP seen \xe2\x80\x94 bridge OK\n'
		return 0
	fi
	printf 'WARNING: no DHCP from Pi (%s) in %ss\n' "$pi_mac" "$timeout" >&2
	return 1
}

# 2-4. Start UART capture before power-on, then power on the Pi and watchdog
# its DHCP. Starting capture after DHCP can miss the fast firmware/plo/kernel
# handoff entirely, leaving an empty or garbage-only log.
"$repo/scripts/pi_power_off.sh" >/dev/null 2>&1 || true

# Kill any orphan picocom/tio still holding /dev/ttyUSB0 from a prior
# aborted cycle. Without this, the new picocom in capture-rpi4b-uart.sh
# fails with "FATAL: cannot lock /dev/ttyUSB0: Resource temporarily
# unavailable" and the cycle aborts with an empty log file.
if command -v fuser >/dev/null 2>&1 && [ -e /dev/ttyUSB0 ]; then
	# `fuser -k` sends SIGKILL by default; -TERM gives the picocom EXIT
	# handler a chance to release the kernel lock cleanly.
	fuser -k -TERM /dev/ttyUSB0 >/dev/null 2>&1 || true
	sleep 1
	fuser -k -KILL /dev/ttyUSB0 >/dev/null 2>&1 || true
fi

sleep "$power_settle_secs"

exit_ms=$(( capture_secs * 1000 ))
capture_args=( --log "$log_path" --exit-after "$exit_ms" )
if [ -n "$uart_baud" ]; then
	capture_args+=( --baud "$uart_baud" )
fi
printf 'capturing UART for %ss -> %s\n' "$capture_secs" "$log_path"
"$repo/scripts/capture-rpi4b-uart.sh" "${capture_args[@]}" &
capture_pid=$!
sleep 1
if ! kill -0 "$capture_pid" 2>/dev/null; then
	wait "$capture_pid" || true
	printf 'ERROR: UART capture exited before Pi power-on; aborting test cycle\n' >&2
	exit 1
fi

# HDMI snapshots: take a "start" frame right after the cycle decides to
# power on the Pi (will likely be black/static at this exact instant —
# but it's a useful baseline that confirms the grabber is functioning),
# then kick off the periodic loop.
if [ -e "$hdmi_grabber" ]; then
	mkdir -p "$hdmi_dir"
	hdmi_grab_one "$(hdmi_label_base)-start.png" || true
	start_hdmi_periodic "$hdmi_interval"
fi

# Power on the Pi and watchdog its DHCP. On timeout, recover the
# bridge by restarting the VM — but KEEP the Pi powered on across the
# recovery, so en7 stays "active" (link up) while socket_vmnet rebuilds
# its BPF capture. Powering the Pi off would make en7 go inactive, and a
# fresh socket_vmnet that comes up against an inactive en7 wedges the
# bridge again as soon as link returns.
pre_lines=$(snapshot_log_lines)
"$repo/scripts/pi_power_on.sh"

if [ "$dhcp_wait_secs" -gt 0 ]; then
	if ! watch_for_dhcp "$dhcp_wait_secs" "$pre_lines"; then
		if [ "$skip_bridge_recovery" = 1 ]; then
			printf 'ERROR: DHCP timeout, --skip-bridge-recovery set; aborting\n' >&2
			exit 1
		fi
		printf 'attempting bridge recovery (VM restart, Pi stays on)...\n' >&2
		"$repo/scripts/netboot-bridge-recover.sh"
		# After VM restart, dnsmasq.log was appended to by the new
		# instance — re-snapshot so we only count truly new lines.
		pre_lines=$(snapshot_log_lines)
		if ! watch_for_dhcp "$dhcp_wait_secs" "$pre_lines"; then
			printf 'ERROR: Pi still not DHCPing after VM restart; check\n' >&2
			printf '  - en7 USB-C ethernet adapter is plugged in\n' >&2
			printf '  - cable to the Pi is connected\n' >&2
			printf '  - Pi is being powered (try pi_power_on.sh manually)\n' >&2
			exit 1
		fi
	fi
fi

wait "$capture_pid" || true

# 5. Power off handled by EXIT trap.

if [ -s "$log_path" ]; then
	printf '\n=== test cycle complete ===\n'
	printf 'log:   %s\n' "$log_path"
	if [[ "$(uname -s)" == "Darwin" ]]; then
		printf 'bytes: %s\n' "$(stat -f %z "$log_path")"
	else
		printf 'bytes: %s\n' "$(stat -c %s "$log_path")"
	fi

	# Post-cycle health check. The most common silent failure is that the
	# CALLER (an agent's Bash tool, or someone's terminal) SIGTERMed the
	# whole script before the watchdog in capture-rpi4b-uart.sh fired —
	# e.g. the Claude Code Bash tool's default 120 s timeout cutting off a
	# 90 s capture that's actually meant to run ~170 s end-to-end. The
	# symptom is a truncated UART log that stops at the kernel banner or
	# `smp: tick+15s` without any of the user-space prints we were after.
	# Flag stage progress explicitly so this is impossible to miss.
	have_phx_banner=0
	have_psh_prompt=0
	have_lwip_line=0
	if grep -aq "Phoenix-RTOS microkernel" "$log_path"; then have_phx_banner=1; fi
	if grep -aq "(psh)% " "$log_path"; then have_psh_prompt=1; fi
	if grep -aq "lwip: genet\|/sbin/lwip " "$log_path"; then have_lwip_line=1; fi

	checkmark() { [ "$1" = 1 ] && printf '✓' || printf '✗'; }
	printf 'stages:\n'
	printf '  [%s] kernel banner (Phoenix-RTOS microkernel)\n' "$(checkmark $have_phx_banner)"
	printf '  [%s] psh prompt    ((psh)%% )\n' "$(checkmark $have_psh_prompt)"
	printf '  [%s] lwip started  (lwip: genet ...)\n' "$(checkmark $have_lwip_line)"

	if [ "$have_phx_banner" = 1 ] && [ "$have_psh_prompt" = 0 ]; then
		cat <<'WARN' >&2

WARNING: Phoenix kernel started but psh prompt never appeared in the
log. Most likely cause: the caller killed this script before the
capture window completed. If you're an agent calling this via the
Bash tool, set timeout >= (capture_secs + 80) * 1000 ms. The Bash
default of 120000 ms cuts off any capture longer than ~40 s after
DHCP.
WARN
	fi
else
	printf '\n=== test cycle complete (log empty) ===\n'
	printf 'log:   %s\n' "$log_path"
	printf 'check that USB-UART is plugged and Pi was powered.\n' >&2
fi
