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
import re
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
        "--stamp",
        action="store_true",
        help="prefix each captured chunk in the LOG with an elapsed-time marker "
        "[T+SS.ss] measured from when the current command was sent. Lets you measure "
        "startup latency of a launched program (grep the log for the marker preceding a "
        "known output line). Off by default so it never disturbs other log consumers.",
    )
    ap.add_argument(
        "--ready-line",
        default=None,
        help="extended regular expression marking the command as READY (e.g. a game's "
             "first presented frame). Until it matches, --max-cmd-secs is only the "
             "deadline for REACHING it; once it matches, capture continues for "
             "--ready-extra-secs more. Without this, a slow start silently eats the "
             "whole window and the run looks like a failure instead of a late start.",
    )
    ap.add_argument(
        "--ready-extra-secs",
        type=float,
        default=60.0,
        help="seconds of capture to guarantee after --ready-line matches (default 60)",
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

        # phase 1.5 (#156): on netboot the NFS-root takeover completes AFTER the psh
        # prompt (plo launches psh as a sibling of the takeover server without gating
        # on it), so commands sent right at the prompt hit the pre-takeover sparse
        # dummyfs root and ENOENT — e.g. "psh: /usr/bin/<bin> not found", the dominant
        # cause of intermittent empty game boots. Wait for the takeover line before
        # sending. SD-boot has no takeover line -> bounded wait then proceed.
        NFS_TAKEOVER_MARKER = b"registered / (takeover)"
        if args.commands and NFS_TAKEOVER_MARKER not in buffered:
            tk_deadline = time.time() + 25
            print(f"waiting up to 25s for NFS takeover {NFS_TAKEOVER_MARKER!r}...")
            while time.time() < tk_deadline:
                data = ser.read(256)
                if not data:
                    continue
                sys.stdout.buffer.write(data)
                sys.stdout.flush()
                log.write(data)
                log.flush()
                buffered.extend(data)
                if NFS_TAKEOVER_MARKER in buffered:
                    print("\n*** NFS root takeover complete — safe to send commands")
                    break
            else:
                print("\n*** takeover marker not seen in 25s (SD-boot / already done?) — proceeding")

        # phase 2: send commands
        for cmd in args.commands:
            time.sleep(args.inter_cmd_secs)
            print(f"\n*** SENDING: {cmd!r}")
            ser.write((cmd + "\n").encode("ascii"))
            ser.flush()

            # Netboot UART input-flake rescue: the command TEXT reliably echoes
            # (the bytes reach psh's input line) but the SUBMITTING newline is
            # dropped ~50% of cold-boot cycles, so psh never runs the command (it
            # just sits on the input line -> the whole cycle looks like it "did
            # nothing"). After a short settle, re-send a BARE newline to submit the
            # already-typed line. This is safe in both cases: if the command
            # already ran, the extra "\n" is just an empty line at the next prompt
            # (it never re-sends the command text, so no double-execution); if the
            # Enter was dropped, this submits the line and rescues the cycle.
            time.sleep(2.0)
            ser.write(b"\n")
            ser.flush()

            # capture response: end after idle-secs of silence OR a hard max
            # (max-cmd-secs) elapsed — the latter prevents an infinite hang when
            # the console is never quiet (genet RXSTATS spam resets the idle timer
            # every ~1 s) or the command launched a never-exiting program.
            cmd_start = time.time()
            quiet_since = cmd_start
            ready_re = re.compile(args.ready_line.encode()) if args.ready_line else None
            ready_at = None
            tail = b""
            # END CONDITIONS, in priority order. The loop condition used to be
            # `while now - quiet_since < idle_secs`, which meant the IDLE timer
            # silently pre-empted everything else: a program that announces
            # nothing while it works (vkQuake compiling 67 pipelines on a cold
            # Mesa shader cache) went quiet for 20 s, the window closed, the Pi
            # was powered off mid-work, and NEITHER the ready-line message nor
            # the max-cmd-secs message printed -- so the run looked like a hang
            # at whatever the last log line happened to be. Cost 2 Pi cycles and
            # a wrong "the GPU archives regressed" conclusion on 2026-09-04.
            #
            # With a --ready-line set, silence is NOT an end condition until the
            # marker matches: max-cmd-secs is the deadline for REACHING it (which
            # is what this code always claimed to do). Without a --ready-line the
            # old idle behaviour is unchanged.
            while True:
                now = time.time()
                if ready_at is not None:
                    # Readiness reached: hold the line for a fixed, useful window
                    # regardless of max-cmd-secs, which was only the budget for
                    # getting here.
                    if now - ready_at >= args.ready_extra_secs:
                        print(f"\n*** ready + {args.ready_extra_secs:.0f}s captured, moving on")
                        break
                elif ready_re is None and (now - quiet_since) >= args.idle_secs:
                    break
                elif args.max_cmd_secs and (now - cmd_start) >= args.max_cmd_secs:
                    if ready_re is not None:
                        print(f"\n*** max-cmd-secs ({args.max_cmd_secs}s) reached WITHOUT "
                              f"matching --ready-line {args.ready_line!r} -- this run never "
                              f"got started, do not read it as a failure of what it was testing")
                    else:
                        print(f"\n*** max-cmd-secs ({args.max_cmd_secs}s) reached, moving on")
                    break
                data = ser.read(256)
                if data:
                    sys.stdout.buffer.write(data)
                    sys.stdout.flush()
                    if args.stamp:
                        log.write(f"\n[T+{time.time() - cmd_start:6.2f}] ".encode("ascii"))
                    log.write(data)
                    log.flush()
                    quiet_since = time.time()
                    if ready_re is not None and ready_at is None:
                        # Match across read boundaries: a marker can straddle two reads.
                        tail = (tail + data)[-512:]
                        if ready_re.search(tail):
                            ready_at = time.time()
                            # A program that goes QUIET after announcing itself
                            # would otherwise be cut by the idle timer instead of
                            # getting its --ready-extra-secs; restart the silence
                            # clock so the post-ready window is really granted.
                            quiet_since = ready_at
                            print(f"\n*** ready after {ready_at - cmd_start:.0f}s "
                                  f"(--ready-line matched); capturing "
                                  f"{args.ready_extra_secs:.0f}s more")
        print("\n*** done")
        return 0
    finally:
        log.close()
        ser.close()


if __name__ == "__main__":
    sys.exit(main())
