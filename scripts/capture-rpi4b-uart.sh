#!/usr/bin/env bash

set -euo pipefail

default_image_path="/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img"
default_output_dir="/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b-uart"

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
      pass picocom --exit-after for dry runs or tests;
      if auto mode would choose tio, the helper falls back to picocom
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
		printf 'hint: rerun with --device /dev/cu....\n' >&2
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
			if have_tool tio; then
				tool="tio"
			elif have_tool picocom; then
				tool="picocom"
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

	if [ -n "$exit_after" ] && [ "$tool" = "tio" ]; then
		if have_tool picocom; then
			printf 'warning: tio does not support --exit-after; falling back to picocom for this invocation\n' >&2
			tool="picocom"
		else
			printf 'warning: tio does not support --exit-after and picocom is unavailable\n' >&2
			exit 1
		fi
	fi
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

if [ "$tool" = "tio" ]; then
	cmd=(
		tio
		--baudrate "$baud"
		--databits 8
		--flow none
		--parity none
		--stopbits 1
		--no-reconnect
		--color none
		--log
		--log-file "$log_path"
		"$device"
	)
else
	cmd=(
		picocom
		--baud "$baud"
		--flow n
		--parity n
		--databits 8
		--stopbits 1
		--noinit
		--noreset
		--logfile "$log_path"
	)

	if [ -n "$exit_after" ]; then
		cmd+=(--exit-after "$exit_after")
	fi

	cmd+=("$device")
fi

exec "${cmd[@]}"
