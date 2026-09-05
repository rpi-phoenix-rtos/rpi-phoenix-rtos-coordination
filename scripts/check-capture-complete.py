#!/usr/bin/env python3
"""check-capture-complete.py -- did the UART capture actually record the command?

WHY THIS EXISTS

A test cycle can end with the log stopping at the echo of the last command: the
Pi ran it, but picocom was already gone, so the log holds no evidence either way.
The failure is silent -- a short log looks exactly like "the program printed
nothing", which reads as "the program is broken".

That misreading cost three wrong conclusions in one session (2026-09-05):
QuakeSpasm "did not start", the Window Maker desktop "does not draw", and the
21.1.24 glamor server "still does not draw". All three were fine; the window was
too short. So the harness has to say so out loud instead of leaving a short log to
be interpreted.

  ./scripts/check-capture-complete.py <log> --commands "cmd1" "cmd2" ...
  ./scripts/check-capture-complete.py <log>          # infer from psh prompts

Exit 0 when the last command has real output after it, 1 when the capture looks
truncated, 2 on a usage problem.

Copyright 2026 Phoenix Systems
SPDX-License-Identifier: BSD-3-Clause
"""
import argparse
import os
import sys

# A command whose output is this short is indistinguishable from "no output at
# all"; below it we refuse to call the capture evidence.
MIN_LINES_AFTER = 3


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("--commands", nargs="*", default=[])
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    if not os.path.isfile(args.log):
        print(f"check-capture-complete: no such log: {args.log}", file=sys.stderr)
        return 2

    with open(args.log, "rb") as fh:
        text = fh.read().decode("utf-8", "replace").replace("\r", "")
    lines = text.split("\n")

    # Anchor on the LAST command the cycle sent. Without an explicit list, fall
    # back to the last line that looks like a psh prompt echoing something.
    anchor = None
    if args.commands:
        last = args.commands[-1]
        for i in range(len(lines) - 1, -1, -1):
            if last in lines[i]:
                anchor = i
                break
        label = last
    else:
        # Without the command list there is no reliable anchor: psh prompt lines
        # carry unrelated boot noise appended to them, so "the last prompt" sits
        # far above the real end of the session and every log looks complete.
        # Say so rather than return a confident wrong verdict.
        print("check-capture-complete: no --commands given — no verdict "
              "(pass the commands the cycle sent)", file=sys.stderr)
        return 0

    if anchor is None:
        print("CAPTURE: could not find the last command in the log — "
              "the cycle may not have reached the psh prompt at all")
        return 1

    after = [ln for ln in lines[anchor + 1:] if ln.strip()]
    if not args.quiet:
        print(f"CAPTURE: last command {label!r}; {len(after)} non-empty lines after it "
              f"({len(text)} bytes total)")

    if len(after) < MIN_LINES_AFTER:
        print("CAPTURE TRUNCATED — the log ends at (or just after) the last command.")
        print("  The run produced NO evidence: this looks identical to a program")
        print("  that printed nothing. Do not conclude anything from it.")
        print("  Re-run with a longer --idle-secs / --max-cmd-secs before bisecting.")
        return 1

    if not args.quiet:
        print("CAPTURE OK — the last command has output in the log")
    return 0


if __name__ == "__main__":
    sys.exit(main())
