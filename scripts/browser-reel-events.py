#!/usr/bin/env python3
"""Where the browser showcase's events are in an HDMI recording, from the clip's UART log.

The browser showcase's items (tools/browser/showcase/pi/*.sh) print, just before each browser
starts, the Pi's wall clock:

    BSHOW item=<name> start epoch=<UTC seconds> ...

and the browsers time their own lines from their start (``WPEB t=<ms> ...`` for WPE WebKit,
``WKGB t=<ms> ...`` for WebKitGTK). The recording's file name holds the host's UTC start
(``<YYYYmmdd-HHMMSS>-<label>.mp4``, scripts/record-hdmi.sh), so

    offset in the video = epoch + t/1000 - recording start - lag

where ``lag`` (default 1.0 s) covers the second the file name truncates and ffmpeg's start-up
before its first frame. Both clocks are NTP-set (the Pi's by psh at boot, which psh-interact.py
waits for before it sends a command), so the offsets are good to about a second. Check one anchor
on a frame (``ffmpeg -ss <offset> -i <clip> -frames:v 1 /tmp/f.png``) and pass ``--lag`` if it is
off.

Anchors, per item (``<item>.<anchor>``; N counts from 1, in log order):

    start          the BSHOW start line (the browser is about to start)
    goN            the Nth address the WPE chrome navigated to (``chrome action=go``)
    loadN          the Nth ``load finished uri=`` of that browser (WPE or WebKitGTK)
    backN          the Nth ``chrome action=back``
    quit           ``chrome action=quit``
    download       WebKitGTK ``download started``;  downloaded  ``download finished``
    tabN           the Nth WebKitGTK ``tab switch``

Usage:
    scripts/browser-reel-events.py <clip.mp4> [--log <uart.log>] [--lag S]     # the timeline
    scripts/browser-reel-events.py <clip.mp4> --anchors                         # name=offset lines

The UART log defaults to the cycle's own: artifacts/rpi4b-uart/rpi4b-uart-<local time>-rec-<label>.log
(scripts/record-showcase-clip.sh names the cycle rec-<label>; PHOENIX_UART_DIR moves the directory,
as for psh-interact.py), the one whose start is closest to the recording's. scripts/make-browser-reel.sh uses --anchors to resolve segment starts written
as ``@<item>.<anchor>[+-<seconds>]``.

Copyright 2026 Phoenix Systems
SPDX-License-Identifier: BSD-3-Clause
"""
import argparse
import datetime
import glob
import os
import re
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLIP_RE = re.compile(r"(\d{8}-\d{6})-(.+)\.mp4$")
LOG_RE = re.compile(r"rpi4b-uart-(\d{8}-\d{6})-(.+)\.log$")
START_RE = re.compile(r"BSHOW item=([A-Za-z0-9_-]+) start epoch=([0-9]+(?:\.[0-9]+)?)")
# UI-process lines only: a child process counts its own t= from its own start
EVENTS = [
    ("go", re.compile(r"WPEB t=(\d+) chrome action=go\b.*?uri=(\S+)")),
    ("load", re.compile(r"(?:WPEB|WKGB) t=(\d+) load finished uri=(\S+)")),
    ("back", re.compile(r"WPEB t=(\d+) chrome action=back\b()")),
    ("quit", re.compile(r"WPEB t=(\d+) chrome action=quit\b()")),
    ("download", re.compile(r"WKGB t=(\d+) download started uri=(\S+)")),
    ("downloaded", re.compile(r"WKGB t=(\d+) download finished\b.*?received=(\d+)")),
    ("tab", re.compile(r"WKGB t=(\d+) tab switch page=(\S+)")),
]
SINGLE = ("quit", "download", "downloaded")


def clip_start(clip):
    m = CLIP_RE.search(os.path.basename(clip))
    if not m:
        sys.exit(f"browser-reel-events: {clip}: not a <YYYYmmdd-HHMMSS>-<label>.mp4 recording")
    utc = datetime.datetime.strptime(m.group(1), "%Y%m%d-%H%M%S").replace(tzinfo=datetime.timezone.utc)
    return utc.timestamp(), m.group(2)


def find_log(start, label):
    """The cycle's UART log: rec-<label>, its LOCAL-time stamp nearest the clip's UTC start."""
    best = None
    uart_dir = os.environ.get("PHOENIX_UART_DIR", os.path.join(REPO, "artifacts", "rpi4b-uart"))
    for path in glob.glob(os.path.join(uart_dir, f"rpi4b-uart-*-rec-{label}.log")):
        m = LOG_RE.search(os.path.basename(path))
        if not m:
            continue
        local = datetime.datetime.strptime(m.group(1), "%Y%m%d-%H%M%S").astimezone()
        gap = abs(local.timestamp() - start)
        if gap < 900 and (best is None or gap < best[0]):
            best = (gap, path)
    if best is None:
        sys.exit(f"browser-reel-events: no {uart_dir}/rpi4b-uart-*-rec-{label}.log within 15 min "
                 f"of the recording; pass --log")
    return best[1]


def timeline(log, start, lag):
    """[(offset, anchor, detail)] in log order; anchors are '<item>.<name>'."""
    out, item, epoch, counts, seen = [], None, None, {}, set()
    with open(log, "rb") as f:
        for raw in f:
            line = raw.decode("utf-8", "replace")
            m = START_RE.search(line)
            if m:
                item, epoch, counts = m.group(1), float(m.group(2)), {}
                out.append((epoch - start - lag, f"{item}.start", line.strip()[:120]))
                continue
            if item is None:
                continue
            for name, rx in EVENTS:
                e = rx.search(line)
                if not e:
                    continue
                t, what = int(e.group(1)), e.group(2)
                # "load finished" is printed twice (with and without the uri); keep one per t+uri
                key = (item, name, t, what)
                if key in seen:
                    break
                seen.add(key)
                counts[name] = counts.get(name, 0) + 1
                n = counts[name]
                anchor = f"{item}.{name}" if name in SINGLE and n == 1 else f"{item}.{name}{n}"
                out.append((epoch + t / 1000.0 - start - lag, anchor, what))
                break
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("clip")
    ap.add_argument("--log", help="the UART log (default: the cycle's rec-<label> log)")
    ap.add_argument("--lag", type=float, default=1.0,
                    help="seconds between the file name's UTC second and the first frame (default 1.0)")
    ap.add_argument("--anchors", action="store_true", help="print name=offset lines only")
    a = ap.parse_args()

    start, label = clip_start(a.clip)
    log = a.log or find_log(start, label)
    events = timeline(log, start, a.lag)
    if not events:
        sys.exit(f"browser-reel-events: no 'BSHOW item=… start epoch=' line in {log}: "
                 f"was it recorded with the showcase's items (tools/browser/showcase)?")
    if a.anchors:
        for off, anchor, _ in events:
            print(f"{anchor}={off:.1f}")
        return 0
    print(f"clip  {a.clip}\nlog   {log}\nlag   {a.lag:.1f} s (offset = event wall clock - recording start - lag)")
    print(f"{'offset s':>9}  {'anchor':22s} detail")
    for off, anchor, detail in sorted(events, key=lambda e: e[0]):
        print(f"{off:9.1f}  {anchor:22s} {detail}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
