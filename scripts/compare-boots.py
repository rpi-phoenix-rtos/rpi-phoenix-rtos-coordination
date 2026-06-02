#!/usr/bin/env python3
"""
compare-boots.py — systematically compare N identical Pi4 netboot runs.

Consumes the timestamped UART logs (capture-rpi4b-uart.sh --timestamp) and the
periodic HDMI snapshots produced by a boot-consistency-study.sh batch, and
reports — per boot and across boots:

  * landmark timings: when each well-known boot milestone first appears,
    expressed as an offset from the boot's first firmware line, so two boots
    can be lined up phase-by-phase;
  * stall structure: inter-line time gaps > THRESHOLD seconds (the ~11 s
    pre-settle PCIe-access stalls), their count, total, and where they land;
  * event counts: repeated markers (USBPOOL allocs, xhci timeouts, descriptor
    failures, enumeration outcome, dummyfs init count);
  * HDMI: md5 of each snapshot, the distinct frames per boot, and whether the
    final frame matches across boots.

Why not a raw line-diff? Concurrent userspace processes write the UART through
an UNLOCKED console, so post-userspace lines are interleaved character-by-
character and are NOT byte-reproducible across boots by construction. Landmark
substrings still appear contiguously (they are emitted in one write), so we key
off those plus structural metrics that survive the garble.

Usage:
  python3 scripts/compare-boots.py [--label-prefix boot] [--gap 5.0]
                                   [--uart-dir DIR] [--hdmi-dir DIR]
                                   [--logs f1.log f2.log ...]
"""
import argparse
import datetime as _dt
import glob
import hashlib
import os
import re
import sys

TS_RE = re.compile(r'^\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+)\]\s?(.*)$')
ANSI_RE = re.compile(r'\x1b\[[0-9;]*[A-Za-z]')
# Bare CSI fragments that survive in the logs without the ESC byte.
BARE_CSI_RE = re.compile(r'\[\d{1,3}(;\d{1,3})*[mHABCDKJP]')

# Landmarks: (key, substring). First line whose cleaned text contains the
# substring marks that milestone. Order ~ boot order.
LANDMARKS = [
    ("fw_first",        None),  # special: first timestamped line overall
    ("plo_banner",      "Phoenix-RTOS loader"),
    ("kernel_banner",   "Phoenix-RTOS microkernel"),
    ("vm_pagealloc",    "Initializing page allocator"),
    ("vm_done",         "vm: init done"),
    ("scheduler",       "Initializing thread scheduler"),
    ("syscalls",        "Initializing syscall table"),
    ("primary_ready",   "primary-ready set"),
    ("syspage_start",   "Starting syspage programs"),
    ("dummyfs_root",    "dummyfs: root initialized"),
    ("usb_driver",      "Initializing driver"),
    ("pcie_settled",    "RC_BAR2 LO=0x00000011"),
    ("xhci_caplength",  "caplength="),
    ("usb_roothub",     "root hub"),
    ("usb_enum_fail",   "Enumeration failed"),
    ("genet_mac",       "genet@fd580000: MAC"),
    ("genet_linkup",    "link up:"),
    ("psh_prompt",      "(psh)%"),
]

# Repeated-event counters: (key, substring).
EVENT_MARKERS = [
    ("usbpool_alloc",        "USBPOOL pa="),
    ("xhci_timeout",         "transfer completion timeout"),
    ("descr_fail",           "Fail to get device descriptor"),
    ("enum_fail",            "Enumeration failed"),
    ("dummyfs_initialized",  "dummyfs: initialized"),
    ("rc_bar2_dump",         "pcie: RC_BAR2"),
]


def clean(text):
    text = ANSI_RE.sub("", text)
    text = BARE_CSI_RE.sub("", text)
    return text


def parse_ts(s):
    return _dt.datetime.strptime(s, "%Y-%m-%d %H:%M:%S.%f")


def load_log(path):
    """Return list of (epoch_seconds, cleaned_text, raw_text)."""
    rows = []
    with open(path, "r", errors="replace") as fh:
        for line in fh:
            line = line.rstrip("\n")
            m = TS_RE.match(line)
            if not m:
                # untimestamped line (e.g. capture header) — skip for timing
                continue
            ts = parse_ts(m.group(1)).timestamp()
            raw = m.group(2)
            rows.append((ts, clean(raw), raw))
    return rows


def analyze(path):
    rows = load_log(path)
    if not rows:
        return {"path": path, "empty": True}
    t0 = rows[0][0]
    span = rows[-1][0] - t0

    # Landmark offsets (seconds from first line).
    landmarks = {}
    for key, sub in LANDMARKS:
        if key == "fw_first":
            landmarks[key] = 0.0
            continue
        for ts, ctext, _ in rows:
            if sub in ctext:
                landmarks[key] = round(ts - t0, 3)
                break
        else:
            landmarks[key] = None

    # Stall structure: gaps between consecutive timestamped lines.
    gaps = []
    for i in range(1, len(rows)):
        dt = rows[i][0] - rows[i - 1][0]
        if dt >= 1.0:
            gaps.append((round(rows[i - 1][0] - t0, 2), round(dt, 2),
                         rows[i][1][:70]))

    # Event counts.
    events = {}
    for key, sub in EVENT_MARKERS:
        events[key] = sum(1 for _, ctext, _ in rows if sub in ctext)

    return {
        "path": path,
        "empty": False,
        "lines": len(rows),
        "span": round(span, 2),
        "landmarks": landmarks,
        "gaps": gaps,
        "events": events,
        "last_line": rows[-1][1][:80],
    }


def md5(path):
    h = hashlib.md5()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def hdmi_for_label(hdmi_dir, label):
    pats = [os.path.join(hdmi_dir, f"*-{label}-*.png"),
            os.path.join(hdmi_dir, f"*-{label}.png")]
    files = []
    for p in pats:
        files.extend(glob.glob(p))
    return sorted(set(files))


def fmt(v):
    return "  --  " if v is None else f"{v:6.1f}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--label-prefix", default="boot")
    ap.add_argument("--gap", type=float, default=5.0,
                    help="report gaps >= this many seconds in the stall summary")
    ap.add_argument("--uart-dir", default="artifacts/rpi4b-uart")
    ap.add_argument("--hdmi-dir", default="artifacts/hdmi")
    ap.add_argument("--logs", nargs="*", default=None)
    args = ap.parse_args()

    if args.logs:
        logs = sorted(args.logs)
    else:
        pat = os.path.join(args.uart_dir,
                           f"*netboot-{args.label_prefix}[0-9][0-9].log")
        logs = sorted(glob.glob(pat))
    if not logs:
        print(f"no logs matched (prefix={args.label_prefix}, dir={args.uart_dir})",
              file=sys.stderr)
        return 1

    results = [analyze(p) for p in logs]
    labels = []
    for p in logs:
        m = re.search(rf"({re.escape(args.label_prefix)}\d+)", os.path.basename(p))
        labels.append(m.group(1) if m else os.path.basename(p))

    print(f"=== {len(logs)} boots: {', '.join(labels)} ===\n")

    # Landmark timing table (offset from first line, seconds).
    print("=== LANDMARK OFFSETS (s from first timestamped line) ===")
    hdr = "landmark".ljust(18) + "".join(l.rjust(8) for l in labels)
    print(hdr)
    for key, _ in LANDMARKS:
        row = key.ljust(18)
        vals = [r["landmarks"].get(key) if not r["empty"] else None
                for r in results]
        row += "".join(fmt(v) for v in vals)
        present = [v for v in vals if v is not None]
        if len(present) >= 2:
            spread = max(present) - min(present)
            row += f"   Δ={spread:5.1f}"
        print(row)

    # Totals / events.
    print("\n=== LINE COUNT / SPAN / EVENT COUNTS ===")
    metric_keys = ["lines", "span"] + [k for k, _ in EVENT_MARKERS]
    print("metric".ljust(20) + "".join(l.rjust(9) for l in labels))
    for mk in metric_keys:
        row = mk.ljust(20)
        for r in results:
            if r["empty"]:
                row += "    --   "
                continue
            v = r.get(mk) if mk in ("lines", "span") else r["events"].get(mk, 0)
            row += f"{v:9}" if isinstance(v, int) else f"{v:9.1f}"
        print(row)

    # Stall summary.
    print(f"\n=== STALLS (inter-line gaps >= {args.gap}s) ===")
    for lab, r in zip(labels, results):
        if r["empty"]:
            print(f"{lab}: EMPTY LOG")
            continue
        big = [g for g in r["gaps"] if g[1] >= args.gap]
        total = round(sum(g[1] for g in big), 1)
        print(f"{lab}: {len(big)} gaps >= {args.gap}s, total {total}s stalled, "
              f"span {r['span']}s, last={r['last_line']!r}")
        for at, dt, nxt in big[:12]:
            print(f"    +{at:7.1f}s  gap {dt:5.1f}s  before: {nxt!r}")

    # HDMI comparison.
    print("\n=== HDMI SNAPSHOTS (md5) ===")
    final_hashes = {}
    for lab in labels:
        files = hdmi_for_label(args.hdmi_dir, lab)
        if not files:
            print(f"{lab}: (no HDMI snapshots)")
            continue
        hashes = [(os.path.basename(f), md5(f)) for f in files]
        distinct = sorted(set(h for _, h in hashes))
        final_hashes[lab] = hashes[-1][1]
        print(f"{lab}: {len(files)} frames, {len(distinct)} distinct; "
              f"final={hashes[-1][1][:12]} ({hashes[-1][0]})")
    if len(final_hashes) >= 2:
        uniq_final = sorted(set(final_hashes.values()))
        print(f"\nfinal-frame agreement: {len(uniq_final)} distinct final frame(s) "
              f"across {len(final_hashes)} boots "
              f"=> {'MATCH' if len(uniq_final) == 1 else 'DIFFER'}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
