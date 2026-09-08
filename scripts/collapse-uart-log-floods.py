#!/usr/bin/env python3
"""Collapse runs of identical consecutive lines in a captured UART log.

Why this exists
---------------
`capture-rpi4b-uart.sh` starts the serial tool BEFORE the Pi is powered on, so
that early boot output is not missed. While the Pi is off, the USB-UART driver
re-serves its last buffer instead of blocking, and the serial tool loops on it.
The result is a log that opens with hundreds of thousands of copies of whatever
line the PREVIOUS session happened to end on -- observed repeatedly on
2026-09-07/08 at 227k-368k lines (7+ MB), e.g. 360275 copies of
`vkvid: present 4170` at the head of a Window Maker test log that never ran
vkQuake.

That is a host capture artifact, not target output, and it is actively harmful:
it buried real evidence, and it was twice mistaken for a Phoenix bug (once for a
libphoenix stdio defect, once for a vkQuake diagnostic flood). Collapsing the
runs keeps every distinct line AND the repeat count, so nothing is lost and the
artifact becomes self-labelling instead of misleading.

Deliberately conservative: only runs of >= --min-run identical *consecutive*
lines are collapsed, well above any legitimate repetition a test produces.
Bytes are handled as latin-1 so a log containing binary noise round-trips
unchanged.
"""

import argparse
import os
import sys
import tempfile

MARKER = "[collapse-uart-log-floods: previous line repeated {n} more times]"


def collapse(src, dst, min_run):
    """Stream src -> dst, collapsing identical consecutive runs. Returns stats."""
    runs = 0
    dropped = 0
    prev = None
    count = 0

    def emit():
        nonlocal runs, dropped
        if prev is None:
            return
        dst.write(prev)
        if count > min_run:
            extra = count - 1
            dst.write(MARKER.format(n=extra) + "\n")
            runs += 1
            dropped += extra

    for line in src:
        if line == prev:
            count += 1
            continue
        emit()
        prev = line
        count = 1
    emit()
    return runs, dropped


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log", help="UART log to rewrite in place")
    ap.add_argument("--min-run", type=int, default=200,
                    help="collapse runs longer than this many identical lines (default 200)")
    ap.add_argument("--dry-run", action="store_true",
                    help="report what would change; do not rewrite")
    args = ap.parse_args()

    if not os.path.isfile(args.log):
        print(f"collapse-uart-log-floods: no such file: {args.log}", file=sys.stderr)
        return 1

    before = os.path.getsize(args.log)
    d = os.path.dirname(os.path.abspath(args.log)) or "."

    # Write beside the target so the replace is atomic on the same filesystem;
    # on any failure the original is left untouched.
    fd, tmp = tempfile.mkstemp(dir=d, prefix=".collapse-", suffix=".tmp")
    try:
        with open(args.log, "r", encoding="latin-1", newline="") as src, \
             os.fdopen(fd, "w", encoding="latin-1", newline="") as dst:
            runs, dropped = collapse(src, dst, args.min_run)
        if runs == 0:
            os.unlink(tmp)
            print(f"collapse-uart-log-floods: nothing to collapse ({args.log})")
            return 0
        after = os.path.getsize(tmp)
        if args.dry_run:
            os.unlink(tmp)
            verb = "would collapse"
        else:
            os.replace(tmp, args.log)
            verb = "collapsed"
        print(f"collapse-uart-log-floods: {verb} {runs} run(s), "
              f"dropped {dropped} duplicate line(s), "
              f"{before} -> {after} bytes ({args.log})")
        return 0
    except Exception as exc:  # noqa: BLE001 - never let this lose a capture
        if os.path.exists(tmp):
            os.unlink(tmp)
        print(f"collapse-uart-log-floods: FAILED, log left unchanged: {exc}",
              file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
