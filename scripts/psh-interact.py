#!/usr/bin/env python3
"""Interactive Phoenix-RTOS shell test over UART.

Waits for the psh prompt marker (`psh: readcmd` or a newline-only banner)
in the UART stream, then injects a sequence of commands, capturing the
response. The Pi must already be powered on (use scripts/pi_power_on.sh
or test-cycle-netboot.sh in the background) and dnsmasq/TFTP must be
serving the rebuilt image.

Usage:
    python3 scripts/psh-interact.py [--device /dev/cu.usbserial-XXX]
                                    [--baud 103448]
                                    [--log artifacts/.../psh.log]
                                    [--wait-secs 90]
                                    [--idle-secs 15]
                                    [--commands "help" "ps" "df"]

Defaults match scripts/capture-rpi4b-uart.sh: 103448 baud (post-baud
switch), autodetect /dev/cu.usbserial-*.
"""
from __future__ import annotations

import argparse
import datetime
import glob
import os
import platform
import sys
import time

import serial

# Resolve the coordination repo root from the script location so the log
# default works on any host (macOS+Lima or Linux dev box).
_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
_REPO_ROOT = os.path.dirname(_SCRIPT_DIR)

DEFAULT_BAUD = 115200  # matches test-cycle-netboot.sh firmware profile;
# plo + kernel speak this rate end-to-end despite the firmware
# briefly reprogramming PL011 to 103448 during its own boot.
# Match the visible psh prompt. The earlier "psh: readcmd" debug marker
# was a Pi 4 bring-up addition stripped during the TD-12 boot-speed
# cleanup (2026-05-18); waiting for the prompt itself avoids depending
# on diagnostic prints that should not exist in production.
PSH_PROMPT_MARKER = b"(psh)%"
DEFAULT_COMMANDS = ["help", "ps", "df", "meminfo"]


def autodetect_device():
    host_os = platform.system()
    if host_os == "Darwin":
        candidates = sorted(glob.glob("/dev/cu.usbserial-*"))
        if not candidates:
            sys.exit("no /dev/cu.usbserial-* device found")
        return candidates[0]
    # Linux: prefer persistent /dev/serial/by-id/ symlink (survives replug).
    by_id = sorted(glob.glob("/dev/serial/by-id/*"))
    if by_id:
        return by_id[0]
    candidates = sorted(glob.glob("/dev/ttyUSB*") + glob.glob("/dev/ttyACM*"))
    if not candidates:
        sys.exit("no /dev/ttyUSB* or /dev/ttyACM* device found")
    return candidates[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--device", default=None)
    ap.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    ap.add_argument(
        "--log",
        default=None,
        help="log file path (default artifacts/rpi4b-uart/...psh-interact.log)",
    )
    ap.add_argument(
        "--wait-secs",
        type=int,
        default=120,
        help="seconds to wait for the psh prompt marker after open",
    )
    ap.add_argument(
        "--idle-secs",
        type=int,
        default=20,
        help="seconds of silence (no new UART bytes) after sending the last command, before exiting",
    )
    ap.add_argument(
        "--inter-cmd-secs",
        type=float,
        default=3.0,
        help="seconds to wait between commands",
    )
    ap.add_argument(
        "--max-cmd-secs",
        type=int,
        default=120,
        help="hard cap on per-command capture (seconds), regardless of idle-detection. "
        "Prevents an infinite hang when the console is never quiet (e.g. genet RXSTATS "
        "spam keeps resetting the idle timer) or when a launched program runs forever "
        "(e.g. an X server). 0 = unlimited (old behavior).",
    )
    ap.add_argument(
        "--commands",
        nargs="+",
        default=DEFAULT_COMMANDS,
        help="commands to send to psh (one per arg)",
    )
    args = ap.parse_args()

    device = args.device or autodetect_device()
    timestamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    log_path = args.log or os.path.join(
        os.environ.get(
            "PHOENIX_UART_DIR",
            os.path.join(_REPO_ROOT, "artifacts", "rpi4b-uart"),
        ),
        f"rpi4b-uart-{timestamp}-psh-interact.log",
    )
    os.makedirs(os.path.dirname(log_path), exist_ok=True)

    print(f"device: {device}")
    print(f"baud:   {args.baud}")
    print(f"log:    {log_path}")
    print(f"commands: {args.commands!r}")

    try:
        ser = serial.Serial(
            device, baudrate=args.baud, timeout=0.5, write_timeout=2.0
        )
    except serial.SerialException as e:
        sys.exit(f"open failed: {e}")

    log = open(log_path, "wb")
    buffered = bytearray()
    found_marker = False
    deadline = time.time() + args.wait_secs

    try:
        # phase 1: wait for psh prompt
        print(f"waiting up to {args.wait_secs}s for marker {PSH_PROMPT_MARKER!r}...")
        while time.time() < deadline:
            data = ser.read(256)
            if not data:
                continue
            sys.stdout.buffer.write(data)
            sys.stdout.flush()
            log.write(data)
            log.flush()
            buffered.extend(data)
            if PSH_PROMPT_MARKER in buffered:
                found_marker = True
                break

        if not found_marker:
            print("\n*** marker NOT seen within deadline; exiting", file=sys.stderr)
            return 2

        print(f"\n*** marker seen — sending {len(args.commands)} command(s)")

        # phase 2: send commands
        for cmd in args.commands:
            time.sleep(args.inter_cmd_secs)
            print(f"\n*** SENDING: {cmd!r}")
            ser.write((cmd + "\n").encode("ascii"))
            ser.flush()

            # capture response: end after idle-secs of silence OR a hard max
            # (max-cmd-secs) elapsed — the latter prevents an infinite hang when
            # the console is never quiet (genet RXSTATS spam resets the idle timer
            # every ~1 s) or the command launched a never-exiting program.
            cmd_start = time.time()
            quiet_since = cmd_start
            while time.time() - quiet_since < args.idle_secs:
                if args.max_cmd_secs and (time.time() - cmd_start) >= args.max_cmd_secs:
                    print(f"\n*** max-cmd-secs ({args.max_cmd_secs}s) reached, moving on")
                    break
                data = ser.read(256)
                if data:
                    sys.stdout.buffer.write(data)
                    sys.stdout.flush()
                    log.write(data)
                    log.flush()
                    quiet_since = time.time()
        print("\n*** done")
        return 0
    finally:
        log.close()
        ser.close()


if __name__ == "__main__":
    sys.exit(main())
