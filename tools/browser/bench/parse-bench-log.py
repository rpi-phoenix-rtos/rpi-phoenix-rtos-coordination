#!/usr/bin/env python3
"""parse-bench-log.py -- what a browser benchmark run said, from a UART log or a bench.sh run log.

  parse-bench-log.py LOG... [--run SUBSTR] [--out DIR] [--slowest N]

Reads the lines of tools/browser/bench (bench-common.js framing): "BENCH <bench> <run> <seq> <kind>
<text>" from either channel (the console, "...: CONSOLE LOG BENCH ...", or the title, "WPEB t=..
title BENCH ..."; the same sequence number on both is one line), "BENCH-DONE ...", and bench.sh's
"BENCH-SUM", "BENCH <bench> HUNG|CRASHED|EXITED". A UART line can be corrupted (~1.3 % of lines on
this bench): a chunk that does not parse is reported, not guessed.

Prints per run: the outcome (bench.sh's result, the page's BENCH-DONE fields), the problems
(page errors, test errors and timeouts, Acid3 failures, HUNG/CRASHED with the last progress line),
and the slowest parts (Speedometer: steps by mean sync+async time; JetStream: benchmarks by wall
time, lowest scores). --out DIR writes <run>.json per run: the reassembled JSON parts and every line.

SPDX-License-Identifier: BSD-3-Clause
"""
import argparse
import collections
import json
import os
import re
import sys

LINE = re.compile(r"BENCH (\S+) (\S+) (\d+) (\S+) ?(.*)$")
DONE = re.compile(r"BENCH-DONE (\S+) (.*?) run=(\S+)(?: t=([\d.]+))?\s*$")
SUM = re.compile(r"^BENCH-SUM (.*)$")
FAIL = re.compile(r"BENCH (\S+) (HUNG|CRASHED|EXITED) run=(\S+) (.*)$")
KV = re.compile(r"(\S+?)=(\S*)")


def parse(paths, only):
    runs = collections.OrderedDict()

    def run_of(bench, run):
        if run not in runs:
            runs[run] = {"bench": bench, "lines": {}, "done": None, "sum": None, "fail": None, "bad": 0}
        return runs[run]

    for path in paths:
        with open(path, "rb") as f:
            for raw in f:
                text = raw.decode("utf-8", "replace").rstrip("\r\n")
                m = FAIL.search(text)
                if m:
                    run_of(m.group(1), m.group(3))["fail"] = (m.group(2), m.group(4))
                    continue
                m = SUM.search(text)
                if m:
                    kv = dict(KV.findall(m.group(1)))
                    if "run" in kv:
                        run_of(kv.get("bench", "?"), kv["run"])["sum"] = kv
                    continue
                m = DONE.search(text)
                if m:
                    r = run_of(m.group(1), m.group(3))
                    r["done"] = dict(KV.findall(m.group(2)))
                    if m.group(4):
                        r["done"]["t"] = m.group(4)
                    continue
                m = LINE.search(text)
                if m:
                    bench, run, seq, kind, rest = m.groups()
                    r = run_of(bench, run)
                    r["lines"].setdefault(int(seq), (kind, rest))
    return collections.OrderedDict((k, v) for k, v in runs.items() if not only or only in k)


def assemble(r):
    """the json <part> k/n chunks, in order; None for a part with a missing or broken chunk"""
    parts = collections.defaultdict(dict)
    for seq in sorted(r["lines"]):
        kind, rest = r["lines"][seq]
        if kind != "json":
            continue
        m = re.match(r"(\S+) (\d+)/(\d+) (.*)$", rest)
        if not m:
            r["bad"] += 1
            continue
        parts[m.group(1)][int(m.group(2))] = (int(m.group(3)), m.group(4))
    out = {}
    for name, chunks in parts.items():
        n = max(c[0] for c in chunks.values())
        if sorted(chunks) != list(range(1, n + 1)):
            out[name] = None
            r["bad"] += 1
            continue
        try:
            out[name] = json.loads("".join(chunks[k][1] for k in range(1, n + 1)))
        except ValueError:
            out[name] = None
            r["bad"] += 1
    return out


def lines_of(r, *kinds):
    return [(seq, r["lines"][seq][1]) for seq in sorted(r["lines"]) if r["lines"][seq][0] in kinds]


def report(run, r, parts, slowest):
    s = r["sum"] or {}
    d = r["done"] or {}
    result = s.get("result") or ("DONE" if r["done"] else (r["fail"][0] if r["fail"] else "INCOMPLETE"))
    print("== %s  [%s]  result=%s secs=%s arm=%s" % (run, r["bench"], result, s.get("secs", "?"), s.get("arm", "?")))
    if d:
        print("   done: " + " ".join("%s=%s" % kv for kv in d.items()))
    if s.get("temp_mC"):
        print("   temp_mC=%s throttled=%s" % (s.get("temp_mC"), s.get("throttled")))
    if r["fail"]:
        print("   %s: %s" % r["fail"])
    for seq, text in lines_of(r, "env"):
        print("   env: " + text[:200])
        break
    problems = lines_of(r, "error", "page-error", "page-rejection", "test-error", "test-timeout", "fail", "skip")
    for seq, text in problems[:60]:
        print("   ! %s %s" % (r["lines"][seq][0], text[:220]))
    if len(problems) > 60:
        print("   ! ... %d more" % (len(problems) - 60))
    seqs = sorted(r["lines"])
    if seqs:
        missing = (seqs[-1] - seqs[0] + 1) - len(seqs)
        if missing:
            print("   lines: %d of seq %d..%d (%d not seen: corrupted or lost)" % (len(seqs), seqs[0], seqs[-1], missing))
    if r["bad"]:
        print("   json: %d part(s) incomplete or corrupt" % r["bad"])

    bench = r["bench"]
    if bench == "speedometer":
        for seq, text in lines_of(r, "iteration"):
            print("   iteration " + text)
        times = collections.defaultdict(list)
        for seq, text in lines_of(r, "timing"):
            m = re.match(r"iter=\d+ (\S+) sync_ms=([\d.]+|NaN) async_ms=([\d.]+|NaN)", text)
            if m:
                times[m.group(1)].append(float(m.group(2)) + float(m.group(3)))
        metrics = parts.get("metrics")
        if not times and metrics:
            for name, (mean, _d, _v) in metrics.items():
                if name.count("/") == 1:
                    times[name].append(mean)
        if times:
            print("   slowest steps (mean sync+async ms):")
            for name, v in sorted(times.items(), key=lambda kv: -sum(kv[1]) / len(kv[1]))[:slowest]:
                print("     %9.1f  %s" % (sum(v) / len(v), name))
        suites = lines_of(r, "suite-result")
        if suites:
            print("   suites (mean total ms):")
            for seq, text in sorted(suites, key=lambda st: -float(re.search(r"mean_ms=([\d.]+)", st[1]).group(1))):
                print("     " + text)
        if result != "DONE":
            steps = lines_of(r, "step", "suite")
            if steps:
                print("   last step started: " + steps[-1][1])
    elif bench == "jetstream":
        tests = lines_of(r, "test")
        walls = []
        for seq, text in tests:
            m = re.match(r"\d+/\d+ (\S+) score=(\S+) .*wall_s=([\d.]+)", text)
            if m:
                walls.append((float(m.group(3)), m.group(1), m.group(2)))
        if walls:
            print("   slowest benchmarks (wall s, score):")
            for w, name, score in sorted(walls, reverse=True)[:slowest]:
                print("     %8.1f  %-28s %s" % (w, name, score))
        for seq, text in lines_of(r, "result", "category"):
            print("   " + text)
        if result != "DONE":
            starts = lines_of(r, "test-start")
            if starts:
                print("   last benchmark started: " + starts[-1][1])
    else:
        for seq, text in lines_of(r, "result", "test", "framerate"):
            print("   " + text[:220])


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("logs", nargs="+")
    ap.add_argument("--run", help="only runs whose id contains this")
    ap.add_argument("--out", help="write <run>.json per run here")
    ap.add_argument("--slowest", type=int, default=10)
    args = ap.parse_args()
    runs = parse(args.logs, args.run)
    if not runs:
        print("no BENCH lines")
        return 1
    for run, r in runs.items():
        parts = assemble(r)
        report(run, r, parts, args.slowest)
        if args.out:
            os.makedirs(args.out, exist_ok=True)
            with open(os.path.join(args.out, re.sub(r"[^A-Za-z0-9._-]", "_", run) + ".json"), "w") as f:
                json.dump({"run": run, "bench": r["bench"], "sum": r["sum"], "done": r["done"], "fail": r["fail"],
                           "parts": parts, "lines": {str(k): v for k, v in sorted(r["lines"].items())}}, f, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
