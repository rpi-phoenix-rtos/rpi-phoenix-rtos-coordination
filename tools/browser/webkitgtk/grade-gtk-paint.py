#!/usr/bin/env python3
"""grade-gtk-paint.py -- grade the GTK paint-watch gate (tools/browser/webkitgtk/pi/gtk-paint.sh)
and the MotionMark arms (bench.sh motionmark-quick ... browser=gtk|wpe stats=5) from UART logs.

    tools/browser/webkitgtk/grade-gtk-paint.py <uart log> [<more logs>...] [--skip N] [--frames]

Per arm (between "GFW arm=<a> start" and "GFW arm=<a> end"; MotionMark: per BENCH-SH run id):
  player   the media player's stat lines (WPEB-MEDIA ... stat ...), both browsers: presented fps,
           painted % (pictures the web process's compositor drew / presented), dropped
  ui       webkit-browser: frames the GTK view drew per s (gtk-paint drawn=), GDK paints per s
           (present-stats); wpe-browser: frames the view presented per s (present frames=)
  web      webkit-browser's web process (frame-watch-web, patch 0020): frames sent to the UI and
           FrameDones back per s, compositions per s
  gtk      the paint watch's per-frame times (avg/max ms, weighted by frames), the frame-interval
           histogram (<25/<42/<58/>=58 ms), superseded/dropped/repaints, frame-clock timings
  trace    --frame-trace lines: p50/p90/max per field (--frames: every line)
The first --skip reports (default 2: start-up) and the last one (the browser being stopped) of an
arm are left out of the steady state. A UART line can be corrupt (~1.3 %): lines that do not parse
are counted and reported, never guessed.

SPDX-License-Identifier: BSD-3-Clause
"""

import argparse
import collections
import re
import statistics
import sys

KV = re.compile(r"([A-Za-z_]+)=(\S*)")
TS = re.compile(r"WKGB t=(\d+) ")
WPEB_TS = re.compile(r"WPEB t=(\d+) ")
MEDIA_TS = re.compile(r"WPEB-MEDIA mono=(\d+) id=(\d+) stat ")
GTK_DURATIONS = ("wait", "rx_to_draw", "before", "swap", "draw", "after", "cycle", "dt")
TRACE_FIELDS = ("wait", "before", "swap", "draw", "after", "rx_to_after", "dt")


def kv(text):
    return dict(KV.findall(text))


def num(value, default=None):
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


class Arm:
    def __init__(self, name, browser):
        self.name = name
        self.browser = browser
        self.lines = []
        self.end = None
        self.media = collections.defaultdict(list)  # player id -> [(mono, fields)]
        self.present = []  # webkit: present-stats fields; wpe: present fields (with t)
        self.gtkpaint = []
        self.web = collections.defaultdict(list)  # pid -> [(t, fields)]
        self.frames = []
        self.notes = []
        self.bad = 0
        self.mm = None


def browser_of(name):
    return "gtk" if name.startswith("gtk") or "-gtk-" in name else "wpe"


def parse(paths):
    arms = collections.OrderedDict()
    cur = None
    common = []
    for path in paths:
        with open(path, "rb") as f:
            for raw in f:
                line = raw.decode("utf-8", "replace").rstrip("\r\n")
                m = re.search(r"GFW arm=(\S+) start ", line)
                if m:
                    cur = arms.setdefault(m.group(1), Arm(m.group(1), browser_of(m.group(1))))
                    continue
                m = re.search(r"BENCH-SH run id=(\S+) bench=", line)
                if m:
                    cur = arms.setdefault(m.group(1), Arm(m.group(1), browser_of(m.group(1))))
                    continue
                m = re.search(r"BENCH-SUM (bench=.*)$", line)
                if m:
                    fields = kv(m.group(1))
                    arm = arms.get(fields.get("run", ""))
                    if arm:
                        arm.mm = fields
                    continue
                m = re.search(r"GFW arm=(\S+) end (.*)$", line)
                if m:
                    if m.group(1) in arms:
                        arms[m.group(1)].end = kv(m.group(2))
                    cur = None
                    continue
                for tag in ("egl-early ", "gdk-gl ", "WPEB-WEBKIT swap-chain", "WPEB-WEBKIT gtk-paint import",
                            "ui start ", "ui window shown", "WPEB-WEBKIT dmabuf-export", "frame-pacing"):
                    if tag in line:
                        (cur.notes if cur else common).append(line[line.find(tag):][:300])
                if cur is None:
                    continue
                cur.lines.append(line)
                try:
                    collect(cur, line)
                except (ValueError, KeyError, IndexError):
                    cur.bad += 1
    return arms, common


def collect(arm, line):
    m = MEDIA_TS.search(line)
    if m:
        arm.media[m.group(2)].append((int(m.group(1)), kv(line[m.end():])))
        return
    m = TS.search(line)
    t = int(m.group(1)) if m else None
    rest = line[m.end():] if m else line
    if rest.startswith("present-stats "):
        f = kv(rest)
        f["t"] = t
        arm.present.append(f)
    elif rest.startswith("gtk-paint ") and "import" not in rest[:20]:
        f = kv(rest)
        f["t"] = t
        arm.gtkpaint.append(f)
    elif rest.startswith("frame-watch-web "):
        f = kv(rest)
        arm.web[f["pid"]].append((t, f))
    elif rest.startswith("frame n="):
        arm.frames.append(kv(rest))
    else:
        m = WPEB_TS.search(line)
        if m and line[m.end():].startswith("present frames="):
            f = kv(line[m.end():])
            f["t"] = int(m.group(1))
            arm.present.append(f)


def steady(items, skip):
    """the reports after the first `skip` and before the last one"""
    return items[skip:-1] if len(items) > skip + 1 else []


def player_summary(arm, skip):
    """the busiest player: presented fps, painted %, dropped over its steady stat lines"""
    best = None
    for pid, rows in arm.media.items():
        rows = [(mono, f) for mono, f in rows if num(f.get("presented")) is not None]
        playing = [r for r in rows if num(r[1].get("presented"), 0) > 0]
        s = steady(playing, skip)
        if len(s) < 2:
            continue
        (m0, a), (m1, b) = s[0], s[-1]
        presented = num(b["presented"]) - num(a["presented"])
        painted = num(b["painted"]) - num(a["painted"])
        dropped = num(b["dropped"]) - num(a["dropped"])
        secs = (m1 - m0) / 1000.0
        if secs <= 0:
            continue
        cand = {"id": pid, "secs": secs, "presented_fps": presented / secs, "painted_fps": painted / secs,
                "painted_pct": 100.0 * painted / presented if presented else 0.0, "dropped": dropped,
                "hw": b.get("hw", "?"), "zc": b.get("zc", "-")}
        if best is None or cand["presented_fps"] > best["presented_fps"]:
            best = cand
    return best


def ui_summary(arm, skip):
    out = {}
    if arm.browser == "gtk":
        s = steady(arm.gtkpaint, skip)
        secs = sum(num(f.get("secs"), 0) for f in s)
        if secs:
            out["drawn_fps"] = sum(num(f.get("drawn"), 0) for f in s) / secs
            out["received_fps"] = sum(num(f.get("received"), 0) for f in s) / secs
        p = steady(arm.present, skip)
        psecs = sum(num(f.get("secs"), 0) for f in p)
        if psecs:
            out["gdk_paints_fps"] = sum(num(f.get("paints"), 0) for f in p) / psecs
            out["timings_complete"] = int(sum(num(f.get("timings_complete"), 0) for f in p))
            out["presented_timings"] = int(sum(num(f.get("presented"), 0) for f in p))
    else:
        p = steady(arm.present, skip)
        if p:
            out["present_fps"] = statistics.mean(num(f.get("fps"), 0) for f in p)
    return out


def web_summary(arm, skip):
    """the web process whose frames_sent grew most: frames sent / done / compositions per s"""
    best = None
    for pid, rows in arm.web.items():
        s = steady(rows, skip)
        if len(s) < 2:
            continue
        (t0, a), (t1, b) = s[0], s[-1]
        secs = (t1 - t0) / 1000.0 if t0 is not None and t1 is not None else 0
        if secs <= 0:
            continue
        cand = {"pid": pid}
        for key, name in (("frames_sent", "sent_fps"), ("frames_done", "done_fps"), ("compositions", "compositions_fps"),
                          ("updates", "updates_fps"), ("no_target", "no_target")):
            delta = num(b.get(key), 0) - num(a.get(key), 0)
            cand[name] = delta if key == "no_target" else delta / secs
        cand["last_kind"] = b.get("kind", "?")
        if best is None or cand["sent_fps"] > best["sent_fps"]:
            best = cand
    return best


def gtk_breakdown(arm, skip):
    s = steady(arm.gtkpaint, skip)
    if not s:
        return None
    out = {}
    drawn = [num(f.get("drawn"), 0) for f in s]
    for name in GTK_DURATIONS:
        avgs, maxes, weights = [], [], []
        for f, w in zip(s, drawn):
            v = f.get(name + "_ms")
            if not v or "/" not in v:
                continue
            a, m = v.split("/", 1)
            avgs.append(num(a, 0))
            maxes.append(num(m, 0))
            weights.append(w if name != "cycle" else num(f.get("cycles"), 0))
        if avgs:
            total = sum(weights)
            out[name] = (sum(a * w for a, w in zip(avgs, weights)) / total if total else statistics.mean(avgs), max(maxes))
    hist = [0, 0, 0, 0]
    for f in s:
        parts = f.get("dt_hist", "").split("/")
        if len(parts) == 4:
            hist = [h + int(num(p, 0)) for h, p in zip(hist, parts)]
    out["dt_hist"] = hist
    for key in ("superseded", "dropped", "unknown", "repaints", "fence_waits", "imports", "cycles", "cycles_frame", "frame_done", "received", "drawn"):
        out[key] = int(sum(num(f.get(key), 0) for f in s))
    return out


def trace_summary(arm):
    if not arm.frames:
        return None
    out = {}
    for name in TRACE_FIELDS:
        values = sorted(v for v in (num(f.get(name)) for f in arm.frames) if v is not None and v >= 0)
        if values:
            out[name] = (values[len(values) // 2], values[min(len(values) - 1, int(len(values) * 0.9))], values[-1])
    out["n"] = len(arm.frames)
    return out


def fmt(value, spec=".1f"):
    return "-" if value is None else format(value, spec)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("logs", nargs="+")
    ap.add_argument("--skip", type=int, default=2, help="start-up reports to leave out (default 2)")
    ap.add_argument("--frames", action="store_true", help="print every --frame-trace line")
    args = ap.parse_args()
    arms, common = parse(args.logs)
    if not arms:
        print("no GFW arm or BENCH-SH run in the log(s)")
        return 1

    print("== setup")
    for line in common + [n for a in arms.values() for n in a.notes]:
        if any(k in line for k in ("egl-early", "gdk-gl", "gtk-paint import", "swap-chain", "ui start", "ui window shown")):
            print("  " + line)

    print("\n== side by side (steady state)")
    hdr = f"{'arm':<34} {'player fps':>10} {'painted%':>8} {'drop':>5} {'ui fps':>7} {'gdk/s':>6} {'web sent/s':>10} {'done/s':>7} {'MM':>7}"
    print(hdr)
    print("-" * len(hdr))
    details = []
    for arm in arms.values():
        p = player_summary(arm, args.skip)
        u = ui_summary(arm, args.skip)
        w = web_summary(arm, args.skip) if arm.browser == "gtk" else None
        ui_fps = u.get("drawn_fps") if arm.browser == "gtk" else u.get("present_fps")
        mm = None
        if arm.mm:
            mm = num(arm.mm.get("score"))
        print(f"{arm.name[:34]:<34} {fmt(p and p['presented_fps']):>10} {fmt(p and p['painted_pct']):>8} {fmt(p and p['dropped'], '.0f'):>5}"
              f" {fmt(ui_fps):>7} {fmt(u.get('gdk_paints_fps')):>6} {fmt(w and w['sent_fps']):>10} {fmt(w and w['done_fps']):>7}"
              f" {fmt(mm, '.2f'):>7}")
        details.append((arm, p, u, w))

    for arm, p, u, w in details:
        if arm.browser != "gtk":
            continue
        print(f"\n== {arm.name}: the GTK paint path (ms, avg/max over the steady state)")
        if arm.end:
            print(f"  end rc={arm.end.get('rc', '?')}")
        if p:
            print(f"  player id={p['id']} hw={p['hw']} zc={p['zc']} {p['secs']:.0f} s: presented {p['presented_fps']:.1f}/s,"
                  f" painted {p['painted_fps']:.1f}/s ({p['painted_pct']:.1f} %), dropped {p['dropped']:.0f}")
        if u:
            print(f"  ui: received {fmt(u.get('received_fps'))}/s drawn {fmt(u.get('drawn_fps'))}/s, GDK paints {fmt(u.get('gdk_paints_fps'))}/s,"
                  f" frame-clock timings complete {u.get('timings_complete', '-')}, with a presentation time {u.get('presented_timings', '-')}")
        if w:
            print(f"  web pid={w['pid']}: sent {w['sent_fps']:.1f}/s, FrameDone {w['done_fps']:.1f}/s, compositions {w['compositions_fps']:.1f}/s,"
                  f" rendering updates {w['updates_fps']:.1f}/s, no free target {w['no_target']:.0f}, last kind={w['last_kind']}")
        g = gtk_breakdown(arm, args.skip)
        if g:
            print("  " + "  ".join(f"{n}={g[n][0]:.1f}/{g[n][1]:.1f}" for n in GTK_DURATIONS if n in g))
            h = g["dt_hist"]
            total = sum(h) or 1
            print(f"  frame interval <25:{h[0]} <42:{h[1]} <58:{h[2]} >=58:{h[3]} ms ({100.0 * h[1] / total:.0f} % in 25-42)")
            print(f"  counts: received={g['received']} drawn={g['drawn']} frame_done={g['frame_done']} superseded={g['superseded']}"
                  f" dropped={g['dropped']} unknown={g['unknown']} repaints={g['repaints']} fence_waits={g['fence_waits']}"
                  f" imports={g['imports']} cycles={g['cycles']} cycles_with_frame={g['cycles_frame']}")
        t = trace_summary(arm)
        if t:
            print(f"  trace n={t['n']} (p50/p90/max): " + "  ".join(f"{n}={t[n][0]:.1f}/{t[n][1]:.1f}/{t[n][2]:.1f}" for n in TRACE_FIELDS if n in t))
            if args.frames:
                for f in arm.frames:
                    print("    frame " + " ".join(f"{k}={v}" for k, v in f.items()))
        if arm.bad:
            print(f"  {arm.bad} line(s) did not parse (UART corruption?)")

    print("\n== reading it (pre-registered, see the report)")
    print("  draw/after large, wait small, web sent ~= content fps  -> the UI's per-frame cost (GDK upload+blend, swap): step 3")
    print("  wait/rx_to_draw large (>~8 ms avg), draw small         -> GDK frame-clock latency on the critical path")
    print("  web sent/s < content fps while the UI is idle          -> the web process is starved by the round trip: step 2")
    return 0


if __name__ == "__main__":
    sys.exit(main())
