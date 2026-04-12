#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path


ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[ -/]*[@-~]")


PHASE_PATTERNS = [
    (
        "phoenix_trampoline",
        [
            r"\bTR0\b",
            r"\bTR1\b",
            r"\bTR2\b",
            r"\bTR3\b",
        ],
    ),
    (
        "eeprom_bootloader",
        [
            r"\bBOOT_UART\b",
            r"\bMESS:",
            r"Boot mode:",
            r"Trying partition",
            r"Failed to open partition",
            r"USB-MSD",
            r"SD CARD",
        ],
    ),
    (
        "firmware_second_stage",
        [
            r"\buart_2ndstage\b",
            r"Read config\.txt",
            r"Read start4",
            r"Read fixup4",
            r"Read kernel8\.img",
            r"Read loader\.disk",
            r"dtdebug",
            r"\bbrfs:",
        ],
    ),
    (
        "phoenix_plo",
        [
            r"call: exec go!",
            r"\bgo: enter\b",
            r"hal: jump exit el1",
            r"^A3$",
            r"^KLM$",
            r"\bplo\b",
        ],
    ),
    (
        "phoenix_kernel",
        [
            r"Phoenix-RTOS microkernel",
            r"main: hal init done",
            r"vm: enter",
            r"map: enter",
            r"gtimer:",
            r"gic:",
        ],
    ),
    (
        "phoenix_userspace",
        [
            r"\(psh\)%",
            r"tty0 ready",
            r"console ready",
            r"Available commands:",
            r"psh:",
            r"pl011-tty",
        ],
    ),
    (
        "exception_or_fault",
        [
            r"Exception #",
            r"\bpanic\b",
            r"\babort\b",
            r"\bfault\b",
            r"Unhandled",
        ],
    ),
]


def strip_ansi(text: str) -> str:
    return ANSI_RE.sub("", text).replace("\r", "")


def load_lines(path: Path) -> list[str]:
    raw = path.read_text(encoding="utf-8", errors="replace")
    return [strip_ansi(line).rstrip("\n") for line in raw.splitlines()]


def classify(matches: list[dict[str, object]]) -> str:
    phases = {m["phase"] for m in matches}
    if "phoenix_userspace" in phases:
        return "runtime-shell"
    if "phoenix_kernel" in phases:
        return "kernel-boot"
    if "phoenix_plo" in phases:
        return "plo-boot"
    if "phoenix_trampoline" in phases:
        return "trampoline-copy"
    if "firmware_second_stage" in phases or "eeprom_bootloader" in phases:
        return "firmware-load"
    return "unknown"


def collect_observations(lines: list[str], matches: list[dict[str, object]]) -> list[str]:
    observations: list[str] = []
    stripped = [line for line in lines if line.strip()]
    phases = {str(m["phase"]) for m in matches}

    saw_baud_switch = any("Set PL011 baud rate to 103448.300000 Hz" in line for line in stripped)
    saw_postswitch_phoenix = any(
        phase in phases for phase in ("phoenix_trampoline", "phoenix_plo", "phoenix_kernel", "phoenix_userspace")
    )

    if saw_baud_switch and not saw_postswitch_phoenix:
        observations.append(
            "log reaches the firmware PL011 baud switch but shows no later Phoenix phases; rerun with "
            "capture-rpi4b-uart.sh --profile postswitch"
        )

    return observations


def collect_matches(lines: list[str]) -> list[dict[str, object]]:
    compiled = [
        (phase, [re.compile(pattern) for pattern in patterns])
        for phase, patterns in PHASE_PATTERNS
    ]

    matches: list[dict[str, object]] = []
    for lineno, line in enumerate(lines, start=1):
        if not line.strip():
            continue
        for phase, patterns in compiled:
            if any(pattern.search(line) for pattern in patterns):
                matches.append({"line": lineno, "phase": phase, "text": line})
                break
    return matches


def summarize(path: Path) -> dict[str, object]:
    lines = load_lines(path)
    matches = collect_matches(lines)

    phase_first: dict[str, dict[str, object]] = {}
    for match in matches:
        phase = str(match["phase"])
        if phase not in phase_first:
            phase_first[phase] = match

    interesting_tail = [line for line in lines if line.strip()][-20:]

    return {
        "path": str(path),
        "total_lines": len(lines),
        "failure_class_guess": classify(matches),
        "first_match_per_phase": phase_first,
        "all_matches": matches,
        "interesting_tail": interesting_tail,
        "observations": collect_observations(lines, matches),
    }


def print_text(summary: dict[str, object]) -> None:
    print(f"Log: {summary['path']}")
    print(f"Total lines: {summary['total_lines']}")
    print(f"Failure class guess: {summary['failure_class_guess']}")
    print("First match per phase:")
    first = summary["first_match_per_phase"]
    if not first:
        print("- none")
    else:
        for phase in [
            "phoenix_trampoline",
            "eeprom_bootloader",
            "firmware_second_stage",
            "phoenix_plo",
            "phoenix_kernel",
            "phoenix_userspace",
            "exception_or_fault",
        ]:
            if phase not in first:
                continue
            match = first[phase]
            print(f"- {phase}: line {match['line']}: {match['text']}")
    print("Interesting tail:")
    for line in summary["interesting_tail"]:
        print(f"- {line}")
    observations = summary["observations"]
    if observations:
        print("Observations:")
        for line in observations:
            print(f"- {line}")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Summarize a Raspberry Pi 4 UART capture log for Phoenix bring-up."
    )
    parser.add_argument("log_path", help="path to the UART log file")
    parser.add_argument("--json", action="store_true", help="emit JSON instead of text")
    args = parser.parse_args()

    path = Path(args.log_path)
    if not path.is_file():
        print(f"missing log: {path}", file=sys.stderr)
        return 1

    summary = summarize(path)
    if args.json:
        print(json.dumps(summary, indent=2))
    else:
        print_text(summary)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
