#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""parse-survey.py -- the browser site survey (survey.sh on the Pi) as a markdown report.

    parse-survey.py <uart log | summary.txt>... [--run NONCE] [--logs DIR] [--json]

Reads the SURVEY lines survey.sh prints (each one up to three times: the UART corrupts the odd
line, so the first copy that parses whole is taken, per site) and, from the UART log, what
happened between a site's SURVEY-SITE line and its SURVEY line: kernel fault dumps ("Exception #";
an EL0 dump is printed twice, so EL0 counts are halved) and the pages' console errors. With the
per-site logs (survey.sh writes them to /root/survey/<run>/ on the NFS root; --logs DIR, default
/srv/phoenix-rpi4-nfs-gcc16/root/survey/<run> when it exists) the console errors come from there
instead: those are not subject to UART corruption.

The output is the "Results" section of docs/browser/SITE-SURVEY.md: the run's settings, the
summary, one table row per site, then the problems site by site.
"""

import argparse
import collections
import json
import os
import re
import sys

NFS_SURVEY = "/srv/phoenix-rpi4-nfs-gcc16/root/survey"
RESULTS = ("OK", "TIMEOUT", "CRASH", "HANG", "ERROR")
SURVEY_RE = re.compile(r"SURVEY run=(\S+) site=(\d+) name=(\S+) result=(OK|TIMEOUT|CRASH|HANG|ERROR) (.*?) url=(\S+) title=(.*)$")
SITE_RE = re.compile(r"SURVEY-SITE run=(\S+) site=(\d+) name=(\S+) url=(\S+)")
BEGIN_RE = re.compile(r"SURVEY-SH begin run=(\S+) (.*)$")
NET_RE = re.compile(r"SURVEY-SH net (.*)$")
SUM_RE = re.compile(r"SURVEY-SUM run=(\S+) (.*)$")
CONSOLE_ERR_RE = re.compile(r"CONSOLE( [A-Z][A-Za-z]*)?( [A-Z]+)? ERROR( |$)")
FAULT_RE = re.compile(r"Exception #\d+: (.*?)(?: \((EL\d)\))?\s*$")
# the fields every intact SURVEY line has, before url=
REQUIRED = ("load_ms", "http", "console_errors", "js_errors", "webprocess_rss_kb", "stalls", "rc", "end", "reason", "wall_s")


def read_lines(path):
    with open(path, "rb") as f:
        for raw in f:
            yield raw.decode("utf-8", errors="replace").rstrip("\r\n")


def parse_kv(text):
    kv = {}
    for word in text.split():
        if "=" in word:
            k, v = word.split("=", 1)
            kv[k] = v
    return kv


def scan(paths):
    """Every run seen: its begin/net/sum lines, its sites' SURVEY lines and UART windows."""
    runs = collections.OrderedDict()

    def run(nonce):
        return runs.setdefault(nonce, {"begin": None, "net": None, "sum": None, "sites": {}, "windows": {}, "order": []})

    for path in paths:
        current = None  # (run, site) whose window is open
        last_run = None
        for line in read_lines(path):
            m = BEGIN_RE.search(line)
            if m:
                r = run(m.group(1))
                r["begin"] = r["begin"] or parse_kv(m.group(2).split(" args=")[0])
                last_run = m.group(1)
                continue
            m = NET_RE.search(line)
            if m and last_run:
                runs[last_run]["net"] = runs[last_run]["net"] or m.group(1)
                continue
            m = SITE_RE.search(line)
            if m:
                current = (m.group(1), int(m.group(2)))
                run(m.group(1))["windows"].setdefault(current[1], {"faults": [], "console_errors": [], "lines": 0})
                continue
            m = SURVEY_RE.search(line)
            if m:
                nonce, site = m.group(1), int(m.group(2))
                kv = parse_kv(m.group(5))
                if all(k in kv for k in REQUIRED):
                    r = run(nonce)
                    if site not in r["sites"]:
                        r["sites"][site] = dict(kv, site=site, name=m.group(3), result=m.group(4), url=m.group(6),
                                                title=m.group(7).strip())
                        r["order"].append(site)
                if current == (nonce, site):
                    current = None
                continue
            m = SUM_RE.search(line)
            if m:
                run(m.group(1))["sum"] = run(m.group(1))["sum"] or parse_kv(m.group(2))
                continue
            if current:
                w = runs[current[0]]["windows"][current[1]]
                w["lines"] += 1
                f = FAULT_RE.search(line)
                if f:
                    w["faults"].append((f.group(1), f.group(2) or ""))
                elif CONSOLE_ERR_RE.search(line):
                    w["console_errors"].append(line)
    return runs


def faults_of(window):
    """The kernel fault dumps in a window, EL0 ones counted once (they are printed twice)."""
    el0 = sum(1 for _, el in window["faults"] if el == "EL0")
    other = len(window["faults"]) - el0
    kinds = collections.Counter(kind for kind, _ in window["faults"])
    return (el0 + 1) // 2 + other, ", ".join(sorted(kinds))


def console_errors_from_log(path):
    errors = []
    try:
        for line in read_lines(path):
            if CONSOLE_ERR_RE.search(line):
                errors.append(line)
    except OSError:
        return None
    return errors


def top_errors(lines, n=3):
    """The commonest console error messages, without the URL:line:col prefix."""
    msgs = collections.Counter()
    for line in lines:
        i = line.find("CONSOLE ")
        msgs[line[i:] if i >= 0 else line] += 1
    return msgs.most_common(n)


def secs(ms):
    try:
        return f"{int(ms) / 1000:.1f}"
    except ValueError:
        return "–"


def mb(kb):
    try:
        return f"{int(kb) / 1024:.0f}"
    except ValueError:
        return "–"


def cell(text, limit=60):
    text = text.replace("|", "\\|")
    return text if len(text) <= limit else text[: limit - 1] + "…"


def report(nonce, r, logs_dir):
    out = []
    begin = r["begin"] or {}
    out.append(f"### Run `{nonce}`")
    out.append("")
    if begin:
        settings = " ".join(f"{k}={v}" for k, v in begin.items() if k not in ("out", "args"))
        out.append(f"- Settings: `{settings}`")
    if r["net"]:
        out.append(f"- Network check: `{r['net']}`")
    if logs_dir:
        out.append(f"- Per-site logs and snapshots: `{logs_dir}` (on the Pi: `/root/survey/{nonce}/`)")
    sites = [r["sites"][s] for s in sorted(r["sites"])]
    counts = collections.Counter(s["result"] for s in sites)
    expected = int(begin["sites"]) if begin.get("sites", "").isdigit() else len(sites)
    missing = expected - len(sites)
    loads = [int(s["load_ms"]) for s in sites if s["result"] == "OK" and s["load_ms"].isdigit()]
    loads.sort()
    median = loads[len(loads) // 2] if loads else None
    summary = ", ".join(f"{counts.get(k, 0)} {k}" for k in RESULTS)
    out.append(f"- Result: **{counts.get('OK', 0)}/{expected} OK** ({summary}" + (f", {missing} without a SURVEY line" if missing > 0 else "") + ")")
    if loads:
        out.append(f"- Load time of the OK sites (`load started` → `load finished`): median {median / 1000:.1f} s, "
                   f"max {loads[-1] / 1000:.1f} s")
    rss = [int(s["webprocess_rss_kb"]) for s in sites if s["webprocess_rss_kb"].isdigit()]
    if rss:
        out.append(f"- Web process footprint (peak per site, WTF memoryFootprint, overcounts): median {sorted(rss)[len(rss) // 2] / 1024:.0f} MB, "
                   f"max {max(rss) / 1024:.0f} MB")
    total_faults = 0
    rows = []
    problems = []
    for s in sites:
        w = r["windows"].get(s["site"], {"faults": [], "console_errors": [], "lines": 0})
        nfaults, kinds = faults_of(w)
        total_faults += nfaults
        log_errors = None
        if logs_dir:
            log_errors = console_errors_from_log(os.path.join(logs_dir, f"{s['site']:02d}-{s['name']}.log"))
        errors = log_errors if log_errors is not None else w["console_errors"]
        png = s.get("snapshot", "-")
        snap = "png" if png not in ("-", "error") else png
        rows.append("| {site} | {name} | **{result}** | {load} | {commit} | {http} | {js} | {ce} | {rss} | {stalls} | {faults} | {snap} | {title} |".format(
            site=s["site"], name=s["name"], result=s["result"], load=secs(s["load_ms"]), commit=secs(s.get("commit_ms", "-")),
            http=s["http"], js=s["js_errors"], ce=s["console_errors"], rss=mb(s["webprocess_rss_kb"]), stalls=s["stalls"],
            faults=nfaults or "", snap=snap, title=cell(s["title"] or "–")))
        if s["result"] != "OK" or nfaults or (s["http"].isdigit() and int(s["http"]) >= 400) or s["stalls"] != "0":
            lines = [f"- **{s['site']} {s['name']}** ({s['url']}): {s['result']}, reason `{s['reason']}`, end `{s['end']}`, "
                     f"rc {s['rc']}, HTTP {s['http']}, wall {s['wall_s']} s, stalls {s['stalls']}, unresponsive {s.get('unresponsive', '?')}"]
            if nfaults:
                lines.append(f"  - kernel faults: {nfaults} ({kinds})")
            for msg, n in top_errors(errors):
                lines.append(f"  - console ×{n}: `{cell(msg, 160)}`")
            problems.extend(lines)
    out.append(f"- Kernel fault dumps during the survey: {total_faults}")
    out.append("")
    out.append("| # | site | result | load s | commit s | HTTP | JS err | console err | web MB | stalls | faults | snap | title |")
    out.append("|---|---|---|---|---|---|---|---|---|---|---|---|---|")
    out.extend(rows)
    if problems:
        out.append("")
        out.append("**Problems** (not OK, HTTP ≥ 400, stall reports or kernel faults):")
        out.append("")
        out.extend(problems)
    out.append("")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("inputs", nargs="+", help="UART log(s) and/or summary.txt files")
    ap.add_argument("--run", help="the run (nonce) to report; default: the last one seen")
    ap.add_argument("--logs", help=f"the run's per-site logs (default {NFS_SURVEY}/<run> if it exists)")
    ap.add_argument("--json", action="store_true", help="the parsed rows as JSON instead of markdown")
    args = ap.parse_args()

    runs = scan(args.inputs)
    runs = collections.OrderedDict((k, v) for k, v in runs.items() if v["sites"])
    if not runs:
        sys.exit("parse-survey: no SURVEY lines in " + ", ".join(args.inputs))
    nonce = args.run or next(reversed(runs))
    if nonce not in runs:
        sys.exit(f"parse-survey: no run {nonce} (runs: {', '.join(runs)})")
    r = runs[nonce]
    logs_dir = args.logs or os.path.join(NFS_SURVEY, nonce)
    if not os.path.isdir(logs_dir):
        logs_dir = None
    if args.json:
        json.dump({"run": nonce, "begin": r["begin"], "net": r["net"], "sum": r["sum"],
                   "sites": [r["sites"][s] for s in sorted(r["sites"])]}, sys.stdout, indent=1)
        print()
        return
    print(report(nonce, r, logs_dir))


if __name__ == "__main__":
    main()
