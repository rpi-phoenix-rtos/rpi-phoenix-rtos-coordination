#!/usr/bin/env bash

set -euo pipefail

# Repo-relative defaults so the helper works from any host (macOS+Lima or
# Linux dev box). Override via env or --log / --output-dir.
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
default_image_path="${RPI4B_SDIMG_PATH:-${repo_root}/artifacts/rpi4b/rpi4b-sd.img}"
default_output_dir="${RPI4B_UART_DIR:-${repo_root}/artifacts/rpi4b-uart}"

baud="115200"
baud_set=0
device=""
exit_after=""
label=""
list_only=0
log_path=""
output_dir="$default_output_dir"
tool="${RPI4B_UART_TOOL:-auto}"
profile="firmware"
# Per-line timestamping (millisecond precision) is OPT-IN via
# --timestamp. Default OFF because of pipe-buffering issues that drop
# post-fbcon: ok content during long lab captures. Implementation:
#   tio     — native --timestamp; stdout has [HH:MM:SS.mmm] prefixes
#             (BUFFERING BUG: tio 3.9 stdout block-buffers even with
#             stdbuf -oL, captures stall after the first few seconds.)
#   picocom — pipe stdout through ts(1) (moreutils); also block-buffers
#             past the first kilobyte if the Pi UART is slow.
# When timestamping is on, the log file contains the timestamp-prefixed
# stream (tio: directly via stdout redirect; picocom: via ts pipe).
# When off, picocom writes raw bytes to --logfile (lossless).
timestamp=0

usage() {
	cat <<'EOF'
Usage: capture-rpi4b-uart.sh [options]

Options:
  --device PATH
      explicit serial device, for example /dev/cu.usbserial-0001
  --tool auto|tio|picocom
      serial tool selection, default auto
  --profile firmware|postswitch
      capture profile, default firmware
  --baud N
      baud rate, default 115200
  --log PATH
      explicit log path
  --output-dir PATH
      directory for auto-created logs
  --label TEXT
      short label added to the auto-created log filename
  --list
      list candidate macOS USB serial devices and exit
  --exit-after MSEC
      stop capture after this many milliseconds using the helper watchdog
  --timestamp
      enable per-line millisecond timestamps (currently has pipe-buffer
      issues that drop slow UART content; default: off)
  --no-timestamp
      disable per-line millisecond timestamps (default)
  --help
      show this help

Notes:
  - firmware profile uses 115200 8N1
  - postswitch profile uses 103448 8N1 for the current Pi 4 PL011 baud switch
  - preferred device path form on macOS is /dev/cu.*
  - auto mode prefers tio if it is installed, else picocom
  - exit tio with Ctrl-T Q
  - exit picocom with Ctrl-A Ctrl-X
EOF
}

apply_profile() {
	case "$profile" in
		firmware)
			if [ "$baud_set" -eq 0 ]; then
				baud="115200"
			fi
			;;
		postswitch)
			if [ "$baud_set" -eq 0 ]; then
				baud="103448"
			fi
			;;
		*)
			printf 'unknown --profile value: %s\n' "$profile" >&2
			exit 1
			;;
	esac
}

list_candidates() {
	local dev base seen=""
	local host_os
	host_os="$(uname -s)"

	# macOS: /dev/cu.* with USB-ish names
	# Linux: /dev/ttyUSB* (FTDI, CH340, CP210x) and /dev/ttyACM* (CDC-ACM)
	if [ "$host_os" = "Darwin" ]; then
		for dev in /dev/cu.*; do
			[ -e "$dev" ] || continue
			base="$(basename "$dev")"
			case "$base" in
				*usb*|*USB*|*serial*|*Serial*|*SLAB*|*wch*|*UART*|*uart*|*modem*|*FT*)
					case " $seen " in
						*" $dev "*) ;;
						*)
							printf '%s\n' "$dev"
							seen="$seen $dev"
							;;
					esac
					;;
			esac
		done
	else
		# Linux. Prefer the persistent /dev/serial/by-id/ symlinks when
		# they exist (survive replug); fall back to /dev/ttyUSB*/ttyACM*.
		if [ -d /dev/serial/by-id ]; then
			for dev in /dev/serial/by-id/*; do
				[ -e "$dev" ] || continue
				# resolve to the real /dev/ttyUSBN path
				real="$(readlink -f "$dev")"
				case " $seen " in
					*" $real "*) ;;
					*) printf '%s\n' "$real"; seen="$seen $real" ;;
				esac
			done
		fi
		for dev in /dev/ttyUSB* /dev/ttyACM*; do
			[ -e "$dev" ] || continue
			case " $seen " in
				*" $dev "*) ;;
				*) printf '%s\n' "$dev"; seen="$seen $dev" ;;
			esac
		done
	fi
}

autodetect_device() {
	local count=0 candidate=""

	while IFS= read -r candidate; do
		[ -n "$candidate" ] || continue
		device="$candidate"
		count=$((count + 1))
	done <<EOF
$(list_candidates)
EOF

	if [ "$count" -eq 0 ]; then
		printf 'no candidate USB serial devices found\n' >&2
		printf 'hint: try --list after plugging the adapter in\n' >&2
		exit 1
	fi

	if [ "$count" -gt 1 ]; then
		printf 'multiple candidate USB serial devices found\n' >&2
		list_candidates >&2
		if [ "$(uname -s)" = "Darwin" ]; then
			printf 'hint: rerun with --device /dev/cu....\n' >&2
		else
			printf 'hint: rerun with --device /dev/ttyUSB0 (or /dev/serial/by-id/...)\n' >&2
		fi
		exit 1
	fi
}

safe_label() {
	printf '%s' "$1" | tr ' /' '__' | tr -cd '[:alnum:]_.-'
}

have_tool() {
	command -v "$1" >/dev/null 2>&1
}

select_tool() {
	case "$tool" in
	auto)
			# Default: picocom with --logfile (raw bytes to disk, no
			# timestamps). This is the lossless path that's been
			# validated against real Pi 4 boots. Timestamping paths
			# (tio --timestamp or picocom | ts) currently drop slow
			# UART output past ~1 KB due to pipe buffering — opt in
			# only when you accept that tradeoff.
			if [ "$timestamp" -eq 1 ] && have_tool picocom && have_tool ts; then
				tool="picocom"
			elif have_tool picocom; then
				tool="picocom"
			elif have_tool tio; then
				tool="tio"
			else
				printf 'missing serial tool: neither tio nor picocom is installed\n' >&2
				exit 1
			fi
			;;
		tio|picocom)
			if ! have_tool "$tool"; then
				printf 'requested serial tool is not installed: %s\n' "$tool" >&2
				exit 1
			fi
			;;
		*)
			printf 'unknown --tool value: %s\n' "$tool" >&2
			exit 1
			;;
	esac

}

while [ $# -gt 0 ]; do
	case "$1" in
		--device)
			device="$2"
			shift 2
			;;
		--tool)
			tool="$2"
			shift 2
			;;
		--profile)
			profile="$2"
			shift 2
			;;
		--baud)
			baud="$2"
			baud_set=1
			shift 2
			;;
		--log)
			log_path="$2"
			shift 2
			;;
		--output-dir)
			output_dir="$2"
			shift 2
			;;
		--label)
			label="$2"
			shift 2
			;;
		--list)
			list_only=1
			shift
			;;
		--exit-after)
			exit_after="$2"
			shift 2
			;;
		--no-timestamp)
			timestamp=0
			shift
			;;
		--timestamp)
			timestamp=1
			shift
			;;
		--help|-h)
			usage
			exit 0
			;;
		*)
			printf 'unknown option: %s\n' "$1" >&2
			usage >&2
			exit 1
			;;
	esac
done

if [ "$list_only" -eq 1 ]; then
	candidates="$(list_candidates)"
	if [ -n "$candidates" ]; then
		printf '%s\n' "$candidates"
	else
		printf 'warning: no candidate USB serial devices found\n' >&2
		if have_tool tio; then
			printf 'info: full tio device list follows\n' >&2
			tio --list
		fi
	fi
	exit 0
fi

apply_profile
select_tool

if [ -z "$device" ]; then
	autodetect_device
fi

if [ ! -e "$device" ]; then
	printf 'missing serial device: %s\n' "$device" >&2
	exit 1
fi

mkdir -p "$output_dir"

if [ -z "$log_path" ]; then
	timestamp="$(date +%Y%m%d-%H%M%S)"
	log_name="rpi4b-uart-$timestamp"
	if [ -n "$label" ]; then
		log_name="${log_name}-$(safe_label "$label")"
	fi
	log_path="${output_dir}/${log_name}.log"
fi

meta_path="${log_path}.meta.txt"
image_sha256="unknown"

if [ -f "$default_image_path" ]; then
	image_sha256="$(shasum -a 256 "$default_image_path" | awk '{print $1}')"
fi

cat > "$meta_path" <<EOF
timestamp: $(date -u +%Y-%m-%dT%H:%M:%SZ)
serial_device: $device
serial_tool: $tool
baud: $baud
profile: $profile
image_path: $default_image_path
image_sha256: $image_sha256
note: use Ctrl-T Q for tio or Ctrl-A Ctrl-X for picocom
EOF

printf 'UART device: %s\n' "$device"
printf 'UART tool:   %s\n' "$tool"
printf 'UART profile:%s\n' " $profile"
printf 'UART baud:   %s\n' "$baud"
printf 'UART log:    %s\n' "$log_path"
printf 'UART meta:   %s\n' "$meta_path"
if [ "$tool" = "tio" ]; then
	printf 'Exit:        Ctrl-T Q\n'
else
	printf 'Exit:        Ctrl-A Ctrl-X\n'
fi
if [ -n "$exit_after" ]; then
	printf 'Watchdog:    stop after %s ms\n' "$exit_after"
fi

if [ "$tool" = "tio" ]; then
	# tio's --timestamp adds [HH:MM:SS.mmm] prefixes to every line on
	# stdout. We redirect stdout to the log file (NOT --log-file, which
	# writes raw bytes without timestamps). Setup/disconnect messages
	# from tio itself appear in the log too, but they're tagged with
	# timestamps so they're easy to ignore in summaries.
	cmd=(
		tio
		--baudrate "$baud"
		--databits 8
		--flow none
		--parity none
		--stopbits 1
		--no-reconnect
		--color none
	)
	if [ "$timestamp" -eq 1 ]; then
		cmd+=(--timestamp)
	else
		cmd+=(--log --log-file "$log_path")
	fi
	cmd+=("$device")
else
	# Note: picocom needs to actually configure the TTY to the requested
	# baud, so do NOT pass --noinit. The macOS USB-UART driver retains
	# the previous baud across opens; if a prior `stty` (or a prior tio
	# session at a different rate) left the TTY at e.g. 9600, picocom
	# with --noinit would silently capture at 9600 while the Pi
	# transmits at 115200, producing pure framing-error garbage. The
	# `--baud` argument is honored only when picocom is allowed to init
	# the TTY.
	cmd=(
		picocom
		--baud "$baud"
		--flow n
		--parity n
		--databits 8
		--stopbits 1
		--noreset
	)
	if [ "$timestamp" -ne 1 ]; then
		# Without timestamps, let picocom write raw bytes to its own
		# logfile (no pipeline needed).
		cmd+=(--logfile "$log_path")
	fi
	cmd+=("$device")
fi

if [ -z "$exit_after" ]; then
	if [ "$tool" = "tio" ] && [ "$timestamp" -eq 1 ]; then
		exec stdbuf -oL "${cmd[@]}" > "$log_path" 2>&1
	elif [ "$tool" = "picocom" ] && [ "$timestamp" -eq 1 ]; then
		exec stdbuf -oL "${cmd[@]}" 2>&1 | ts '[%Y-%m-%d %H:%M:%.S]' > "$log_path"
	else
		exec "${cmd[@]}"
	fi
fi

case "$exit_after" in
	''|*[!0-9]*)
		printf 'invalid --exit-after value: %s\n' "$exit_after" >&2
		exit 1
		;;
esac

watchdog_secs=$(( (exit_after + 999) / 1000 ))
[ "$watchdog_secs" -gt 0 ] || watchdog_secs=1

stdin_dir="$(mktemp -d "${TMPDIR:-/tmp}/rpi4b-uart-stdin.XXXXXX")"
stdin_fifo="$stdin_dir/stdin"
mkfifo "$stdin_fifo"
exec 3<>"$stdin_fifo"

if [ "$tool" = "tio" ] && [ "$timestamp" -eq 1 ]; then
	# Redirect tio's timestamp-prefixed stdout to the log file directly.
	# `stdbuf -oL` forces line-buffered stdout so each UART line lands in
	# the log file immediately; without it glibc switches stdout to a 4 KB
	# block buffer when it's redirected to a regular file, and the
	# captured bytes never reach disk before the watchdog kills tio.
	stdbuf -oL "${cmd[@]}" <"$stdin_fifo" > "$log_path" 2>&1 &
elif [ "$tool" = "picocom" ] && [ "$timestamp" -eq 1 ]; then
	# Pipe picocom's stdout through ts(1) to add per-line timestamps.
	# Watchdog targets the bash subshell pid, which propagates SIGTERM
	# to the picocom + ts pipeline. BOTH ends must be line-buffered:
	# picocom's stdout (stdbuf -oL) AND ts's stdout to the log FILE
	# (stdbuf -oL) — without the latter, glibc block-buffers ts's writes
	# to the regular file (4 KB) and the final buffer is lost when the
	# watchdog kills the pipeline (the old "drops post-fbcon content" bug).
	(stdbuf -oL "${cmd[@]}" <"$stdin_fifo" 2>&1 | stdbuf -oL ts '[%Y-%m-%d %H:%M:%.S]' > "$log_path") &
else
	"${cmd[@]}" <"$stdin_fifo" &
fi
capture_pid=$!

(
	sleep "$watchdog_secs"
	if kill -0 "$capture_pid" 2>/dev/null; then
		kill "$capture_pid" 2>/dev/null || true
	fi
) &
watchdog_pid=$!

set +e
wait "$capture_pid"
capture_rc=$?
set -e

kill "$watchdog_pid" 2>/dev/null || true
wait "$watchdog_pid" 2>/dev/null || true
exec 3>&-
rm -rf "$stdin_dir"

if [ "$capture_rc" -ne 0 ]; then
	printf 'UART capture stopped after %ss (capture rc=%s)\n' "$watchdog_secs" "$capture_rc" >&2
	exit 0
fi

exit 0
