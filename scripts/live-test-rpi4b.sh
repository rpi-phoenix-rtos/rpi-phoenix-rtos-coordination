#!/usr/bin/env bash
#
# live-test-rpi4b.sh — INTERACTIVE, human-driven live Raspberry Pi 4 session.
#
# ============================================================================
# THIS SCRIPT IS FOR A HUMAN AT A GRAPHICAL DESKTOP. AGENTS MUST NOT RUN IT.
# ============================================================================
# It BLOCKS indefinitely until you CLOSE the HDMI preview window (or Ctrl-C).
# There is NO time limit anywhere. An agent calling this via a Bash tool will
# hang until its own timeout fires, leave the Pi powered on, and corrupt any
# concurrent test cycle. If you are an agent: do not run this — use
# scripts/test-cycle-netboot.sh instead.
#
# What it does, in one command:
#   1. Ensures the host netboot server (dnsmasq DHCP+TFTP) is up.
#   2. Auto-detects the USB-UART device.
#   3. Powers the Pi on (Meross smart plug via pi_power_on.sh).
#   4. Streams UART to your terminal LIVE (line-buffered), AND tees it to a
#      timestamped log under artifacts/rpi4b-uart/.
#   5. Opens a LIVE HDMI preview window (ffplay) from the USB HDMI grabber so
#      you watch the Pi's framebuffer in real time.
#   6. Blocks until you close the preview window.
#   7. On window-close (or Ctrl-C): stops UART capture, prints the log path,
#      and powers the Pi off. The netboot server is left running unless
#      --stop-netboot is given.
#
# Typical workflow:
#   - Run it, watch boot scroll by on the terminal (UART) and the framebuffer
#     come up in the preview window (HDMI).
#   - Once psh is up, TYPE ON THE USB KEYBOARD PLUGGED INTO THE PI and watch
#     the characters echo both in the preview window and on the UART stream.
#   - When you're done, CLOSE THE PREVIEW WINDOW. The script cleans up and
#     powers the Pi off for you.
#
# Usage:
#   ./scripts/live-test-rpi4b.sh [options]
#
# Options:
#   --uart DEV        explicit serial device (default: auto-detect, same logic
#                     as capture-rpi4b-uart.sh; or set RPI4B_UART_DEV)
#   --grabber DEV     HDMI grabber video device (default: /dev/video4, or set
#                     RPI4B_HDMI_GRABBER)
#   --no-preview      do not open an HDMI window; stream UART only and wait for
#                     Ctrl-C (use this on a headless host with no GUI / no
#                     ffplay/mpv)
#   --sd-boot         boot Phoenix from the Pi's SD card instead of netboot: the
#                     netboot server is brought DOWN (not up) so the firmware's
#                     network-boot times out and falls through to the SD card.
#                     Put a bootable Phoenix SD card in the Pi first. (Network-
#                     first EEPROM means a running netboot server would win, so
#                     it must be down for SD boot.)
#   --stop-netboot    bring the netboot server down during cleanup (default:
#                     leave it running — it is cheap and you may re-run). Implied
#                     by --sd-boot (the server is already down).
#   --help            show this help
#
# Env overrides:
#   RPI4B_UART_DEV       same as --uart
#   RPI4B_HDMI_GRABBER   same as --grabber
#   RPI4B_UART_BAUD      UART baud (default 115200)
#
# Dependencies the user must have installed:
#   - picocom (UART; same tool capture-rpi4b-uart.sh uses)
#   - ffplay (from ffmpeg) for the live preview window; OR mpv as a fallback.
#     Without either, use --no-preview.
#
# NOT set -e: a transient UART hiccup must not kill the interactive session.
set -uo pipefail

repo="${PHOENIX_RPI_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}"

# Honour project-local environment overrides (gitignored), matching the other
# netboot helpers.
if [ -f "$repo/.env.local" ]; then
	set -a
	# shellcheck disable=SC1091
	. "$repo/.env.local"
	set +a
fi

# ---------------------------------------------------------------------------
# Defaults + flags
# ---------------------------------------------------------------------------
uart_dev="${RPI4B_UART_DEV:-}"
grabber="${RPI4B_HDMI_GRABBER:-/dev/video4}"
baud="${RPI4B_UART_BAUD:-115200}"
no_preview=0
stop_netboot=0
sd_boot=0

usage() {
	# Print the top-of-file comment block (everything between the shebang and
	# `set -uo pipefail`) as the help text, so docs live in exactly one place.
	sed -n '2,/^set -uo pipefail$/{/^set -uo pipefail$/d;s/^# \{0,1\}//;p}' "$0"
}

log() { printf '[live-test] %s\n' "$*" >&2; }

while [ $# -gt 0 ]; do
	case "$1" in
		--uart)         uart_dev="$2"; shift 2 ;;
		--grabber)      grabber="$2"; shift 2 ;;
		--no-preview)   no_preview=1; shift ;;
		--sd-boot)      sd_boot=1; shift ;;
		--stop-netboot) stop_netboot=1; shift ;;
		-h|--help)      usage; exit 0 ;;
		*) printf 'unknown option: %s\n' "$1" >&2; usage >&2; exit 1 ;;
	esac
done

have() { command -v "$1" >/dev/null 2>&1; }

# ---------------------------------------------------------------------------
# UART device auto-detection — same preference order as capture-rpi4b-uart.sh:
# persistent /dev/serial/by-id/* symlinks first (survive replug), then
# /dev/ttyUSB* / /dev/ttyACM*. (Linux host only; this lab is Linux.)
# ---------------------------------------------------------------------------
list_uart_candidates() {
	local dev real seen=""
	if [ -d /dev/serial/by-id ]; then
		for dev in /dev/serial/by-id/*; do
			[ -e "$dev" ] || continue
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
}

autodetect_uart() {
	local count=0 candidate=""
	while IFS= read -r candidate; do
		[ -n "$candidate" ] || continue
		uart_dev="$candidate"
		count=$((count + 1))
	done <<EOF
$(list_uart_candidates)
EOF
	if [ "$count" -eq 0 ]; then
		log "no candidate USB serial devices found"
		log "hint: plug the USB-UART adapter in, or pass --uart /dev/ttyUSB0"
		exit 1
	fi
	if [ "$count" -gt 1 ]; then
		log "multiple candidate USB serial devices found:"
		list_uart_candidates >&2
		log "hint: rerun with --uart /dev/ttyUSB0 (or /dev/serial/by-id/...)"
		exit 1
	fi
}

# ---------------------------------------------------------------------------
# Cleanup — runs on EXIT/INT/TERM/HUP. Idempotent (guard flag). Order:
#   1. kill the preview window (so closing the terminal also closes the window)
#   2. stop UART capture: kill picocom, then fuser the device for a guaranteed
#      lock release (belt-and-suspenders, matches test-cycle-netboot.sh)
#   3. print where the log landed
#   4. power the Pi off (the one invariant that must always hold)
#   5. optionally bring the netboot server down
# ---------------------------------------------------------------------------
cleanup_done=0
preview_pid=""
uart_pid=""
log_path=""

cleanup() {
	[ "$cleanup_done" -eq 1 ] && return 0
	cleanup_done=1

	if [ -n "$preview_pid" ] && kill -0 "$preview_pid" 2>/dev/null; then
		kill "$preview_pid" 2>/dev/null || true
		wait "$preview_pid" 2>/dev/null || true
	fi

	if [ -n "$uart_pid" ] && kill -0 "$uart_pid" 2>/dev/null; then
		# SIGTERM lets picocom's own handler release the kernel TTY lock.
		kill -TERM "$uart_pid" 2>/dev/null || true
		wait "$uart_pid" 2>/dev/null || true
	fi
	# Guaranteed release of the device lock for the next run, even if the
	# picocom above was wedged. Best-effort, guarded.
	if have fuser && [ -n "$uart_dev" ] && [ -e "$uart_dev" ]; then
		fuser -k -TERM "$uart_dev" >/dev/null 2>&1 || true
		fuser -k -KILL "$uart_dev" >/dev/null 2>&1 || true
	fi

	if [ -n "$log_path" ] && [ -s "$log_path" ]; then
		log "UART log saved: $log_path"
	elif [ -n "$log_path" ]; then
		log "UART log (empty): $log_path"
	fi

	log "powering Pi off..."
	"$repo/scripts/pi_power_off.sh" >/dev/null 2>&1 \
		|| log "WARNING: pi_power_off.sh failed during cleanup — power the Pi off manually!"

	if [ "$stop_netboot" -eq 1 ]; then
		if [ -x "$repo/scripts/netboot-server-down.sh" ]; then
			log "stopping netboot server (--stop-netboot)..."
			"$repo/scripts/netboot-server-down.sh" >/dev/null 2>&1 || true
		else
			log "--stop-netboot: scripts/netboot-server-down.sh not found, skipping"
		fi
	fi
	log "done."
}
trap cleanup EXIT INT TERM HUP

# ---------------------------------------------------------------------------
# Preflight
# ---------------------------------------------------------------------------
if ! have picocom; then
	log "missing required tool: picocom (used to read the UART)"
	exit 1
fi

# Resolve UART device.
if [ -z "$uart_dev" ]; then
	autodetect_uart
fi
if [ ! -e "$uart_dev" ]; then
	log "serial device not present: $uart_dev"
	exit 1
fi

# Choose the preview tool (unless --no-preview).
preview_kind="none"
if [ "$no_preview" -eq 0 ]; then
	if have ffplay; then
		preview_kind="ffplay"
	elif have mpv; then
		preview_kind="mpv"
		log "ffplay not found; falling back to mpv"
	else
		log "neither ffplay (ffmpeg) nor mpv is installed."
		log "install ffmpeg for the live HDMI window, or rerun with --no-preview"
		log "to stream UART only (stop with Ctrl-C)."
		exit 1
	fi
	if [ ! -e "$grabber" ]; then
		log "HDMI grabber not present: $grabber"
		log "hint: pass --grabber /dev/videoN or rerun with --no-preview"
		exit 1
	fi
fi

# ---------------------------------------------------------------------------
# 1. Netboot server. For netboot: bring it UP (serves the Pi). For --sd-boot:
#    bring it DOWN so the firmware's network-boot times out and the Pi falls
#    through to the SD card (the EEPROM is network-first, so a running server
#    would win over the card).
# ---------------------------------------------------------------------------
if [ "$sd_boot" -eq 1 ]; then
	log "SD-boot: bringing netboot server DOWN so the Pi falls through to the SD card..."
	if [ -x "$repo/scripts/netboot-server-down.sh" ]; then
		"$repo/scripts/netboot-server-down.sh" >/dev/null 2>&1 || true
	fi
	log "make sure a bootable Phoenix SD card is in the Pi (network-boot will time out first, ~15-30s)."
else
	log "ensuring netboot server (dnsmasq DHCP+TFTP) is up..."
	if ! "$repo/scripts/netboot-server-up.sh"; then
		log "ERROR: netboot-server-up.sh failed; cannot serve the Pi. Aborting."
		exit 1
	fi
fi

# ---------------------------------------------------------------------------
# Prepare the UART log path.
# ---------------------------------------------------------------------------
log_dir="${RPI4B_UART_DIR:-$repo/artifacts/rpi4b-uart}"
mkdir -p "$log_dir"
ts="$(date +%Y%m%d-%H%M%S)"
log_path="$log_dir/$ts-live-test.log"

# Free a stale lock from a prior aborted session before we power on, so the
# new picocom can grab the device (matches test-cycle-netboot.sh).
if have fuser && [ -e "$uart_dev" ]; then
	fuser -k -TERM "$uart_dev" >/dev/null 2>&1 || true
	sleep 1
	fuser -k -KILL "$uart_dev" >/dev/null 2>&1 || true
fi

# ---------------------------------------------------------------------------
# 4 (power) — power the Pi on.
# ---------------------------------------------------------------------------
log "powering Pi on..."
if ! "$repo/scripts/pi_power_on.sh"; then
	log "ERROR: pi_power_on.sh failed."
	exit 1
fi

# ---------------------------------------------------------------------------
# 5. Start UART capture in the background: stream to stdout LIVE and tee to the
# log. Both ends line-buffered (stdbuf -oL on picocom AND on tee) so the live
# view doesn't stutter and the log tail isn't lost on power-off.
#
# Background picocom is fed an open-but-empty FIFO as stdin (the proven trick
# from capture-rpi4b-uart.sh): inheriting the terminal would put the TTY in raw
# mode and eat the user's Ctrl-C; </dev/null would let picocom see EOF and exit
# early. The FIFO stays open for the session's lifetime.
#
# Structure note (re-runnability depends on this): we use a simple command +
# process substitution, NOT a `picocom | tee` pipeline, so that $! is picocom
# itself — the process that holds the device lock — and cleanup can kill the
# right PID. tee in the process-sub flushes on EOF when picocom dies.
# ---------------------------------------------------------------------------
stdin_dir="$(mktemp -d "${TMPDIR:-/tmp}/rpi4b-live-stdin.XXXXXX")"
stdin_fifo="$stdin_dir/stdin"
mkfifo "$stdin_fifo"
exec 4<>"$stdin_fifo"

log "UART device: $uart_dev (baud $baud)"
log "UART log:    $log_path"
log "streaming UART live below; framebuffer in the preview window."
printf '\n' >&2

# --noinit is intentionally NOT used: picocom must configure the TTY to $baud
# (the USB-UART driver retains the previous baud across opens). --noreset keeps
# DTR/RTS from bouncing the adapter. Errors on stderr go to /dev/null so they
# don't interleave with the live UART stream on stdout.
stdbuf -oL picocom \
	--baud "$baud" \
	--flow n \
	--parity n \
	--databits 8 \
	--stopbits 1 \
	--noreset \
	"$uart_dev" <"$stdin_fifo" 2>/dev/null \
	> >(stdbuf -oL tee -a "$log_path") &
uart_pid=$!

# Give picocom a moment; if it died immediately (e.g. lock contention), bail.
sleep 1
if ! kill -0 "$uart_pid" 2>/dev/null; then
	log "ERROR: UART reader (picocom) exited immediately — device busy/locked?"
	log "check that no other picocom/tio holds $uart_dev"
	exit 1
fi

# ---------------------------------------------------------------------------
# 6. Open the live HDMI preview window (or, with --no-preview, just wait).
#    Block on the chosen process; closing the window (or Ctrl-C) ends it.
# ---------------------------------------------------------------------------
if [ "$preview_kind" = "none" ]; then
	log "--no-preview: streaming UART only. Press Ctrl-C to stop and power off."
	# Wait on picocom; thanks to the FIFO it won't self-exit, so Ctrl-C is the
	# only way out — which the trap turns into clean cleanup + power-off.
	wait "$uart_pid"
else
	log "opening live HDMI preview window from $grabber — CLOSE THE WINDOW to stop."
	log "tip: once psh is up, type on the USB keyboard on the Pi and watch it echo."
	if [ "$preview_kind" = "ffplay" ]; then
		# Low-latency flags so keypresses echo near-instantly. ffplay's stderr
		# is very chatty (decoder/SDL spam); send it to /dev/null so it doesn't
		# interleave with the live UART on stdout.
		# Pin the capture format to full-HD MJPEG explicitly, rather than inheriting
		# whatever the device's lingering default is. A prior `ffmpeg -video_size`
		# or `v4l2-ctl --set-fmt` (e.g. the flicker-capture harness) persistently
		# changes the v4l2 device default, which would otherwise open this preview
		# tiny/upscaled. MJPEG (not raw YUYV) is what the USB grabber can sustain at
		# 1080p30. Override with RPI4B_HDMI_RES / RPI4B_HDMI_INFMT if needed.
		ffplay \
			-f v4l2 \
			-input_format "${RPI4B_HDMI_INFMT:-mjpeg}" \
			-video_size "${RPI4B_HDMI_RES:-1920x1080}" \
			-framerate 30 \
			-fflags nobuffer \
			-flags low_delay \
			-i "$grabber" \
			-window_title "Pi4 live (close window to power off)" \
			>/dev/null 2>&1 &
		preview_pid=$!
	else
		# mpv fallback. --profile=low-latency keeps echo snappy; --title sets
		# the window title.
		mpv \
			--profile=low-latency \
			--untimed \
			--demuxer-lavf-o=video_size="${RPI4B_HDMI_RES:-1920x1080}",input_format="${RPI4B_HDMI_INFMT:-mjpeg}" \
			--title="Pi4 live (close window to power off)" \
			"av://v4l2:$grabber" \
			>/dev/null 2>&1 &
		preview_pid=$!
	fi

	# Block until the user closes the preview window. No timeout. When it
	# returns, fall through to the EXIT trap which stops UART + powers off.
	wait "$preview_pid"
fi

# Normal end of the interactive session: cleanup runs via the EXIT trap.
exit 0
