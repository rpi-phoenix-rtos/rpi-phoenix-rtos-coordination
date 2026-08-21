#!/usr/bin/env python3
"""Differential-correctness harness for the GNU coreutils 9.5 Phoenix Pi4 port.

Proves the ported /usr/bin/<tool> binaries on Phoenix (real Pi4, netboot) produce
BIT-IDENTICAL output to the SAME GNU coreutils 9.5 built natively on the host, over a
curated corpus of deterministic cases (tools/coreutils-difftest/cases.tsv).

WHY a native GNU build is the reference: the host's default /usr/bin coreutils is
uutils 0.8.0 (a Rust reimplementation) which differs from GNU in formatting/edge cases.
Build GNU 9.5 natively and point --refdir at its src/ directory.

Subcommands:
  host   --refdir DIR         run each case with the native GNU-9.5 binaries -> expected/<id>.out
  gencmds [--pilot]           print one Pi psh command per case (feed to test-cycle-psh-interact)
  check  --log LOG [--pilot]  parse the UART log, normalize, diff vs expected/ -> PASS/FAIL table + RESULTS.md

Correctness invariants:
  * psh tokenizes with strtok(line, " \\t") -- no quote/backslash processing. So the host
    reference is invoked with the SAME whitespace-split argv (a token list, NO shell), and
    the corpus uses no quotes/metachars.
  * host reference runs with LC_ALL=C / LANG=C to match Phoenix's C-locale libc (avoids
    false diffs in sort collation, numfmt/printf decimal separator, etc.).
  * filename normalization: the Pi path /root/cutest/<f> is rewritten to <f> so only the
    real content is compared (host runs with cwd=corpus so it already prints bare names).
  * only \\r is stripped before diffing, so "bit-identical" stays meaningful.
"""
from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys

# psh/teken emit ANSI CSI sequences (e.g. ESC[0J clear-to-end) around the prompt;
# strip them so prompt-line filtering + command matching work on clean text.
ANSI_CSI = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")

HERE = os.path.dirname(os.path.abspath(__file__))
CORPUS = os.path.join(HERE, "corpus")
EXPECTED = os.path.join(HERE, "expected")
PIOUT = os.path.join(HERE, "piout")
CASES = os.path.join(HERE, "cases.tsv")

PI_TOOLDIR = "/usr/bin"
PI_FILEDIR = "/root/cutest"
PSH_PROMPT = "(psh)%"


def load_cases(pilot_only=False):
    """Return [(id, [raw tokens]), ...]. pilot set = first 6 (mix of pure-arg + file-arg)."""
    cases = []
    with open(CASES, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line or line.lstrip().startswith("#"):
                continue
            cid, tokstr = line.split("\t", 1)
            cases.append((cid.strip(), tokstr.split(" ")))
    if pilot_only:
        # A representative pilot: echo, printf(\n), a locale-sensitive sort, a
        # filename-printing file case, and a binary file case.
        want = {"echo", "printf-mixed", "sort", "wc", "sha256sum", "od"}
        cases = [c for c in cases if c[0] in want]
    return cases


def pi_command(tokens):
    """Build the psh command string sent to the Pi for a case."""
    out = [PI_TOOLDIR + "/" + tokens[0]]
    for t in tokens[1:]:
        if t.startswith("@F:"):
            out.append(PI_FILEDIR + "/" + t[3:])
        else:
            out.append(t)
    return " ".join(out)


def host_argv(tokens, refdir):
    """Build the argv list handed directly to the native GNU binary (NO shell)."""
    argv = [os.path.join(refdir, tokens[0])]
    for t in tokens[1:]:
        if t.startswith("@F:"):
            argv.append(t[3:])  # bare basename; host runs with cwd=corpus
        else:
            argv.append(t)
    return argv


def normalize(text, is_pi):
    """Strip only \\r; on the Pi side rewrite /root/cutest/ away so filenames match."""
    text = text.replace("\r", "")
    if is_pi:
        text = text.replace(PI_FILEDIR + "/", "")
    return text


def cmd_host(args):
    os.makedirs(EXPECTED, exist_ok=True)
    env = dict(os.environ)
    env["LC_ALL"] = "C"
    env["LANG"] = "C"
    cases = load_cases()
    for cid, tokens in cases:
        argv = host_argv(tokens, args.refdir)
        proc = subprocess.run(
            argv, cwd=CORPUS, env=env,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
        # Most target tools write to stdout; expr/factor also stdout. Capture stdout
        # (normalized) as the expected output; record exit status alongside.
        out = normalize(proc.stdout.decode("latin-1"), is_pi=False)
        with open(os.path.join(EXPECTED, cid + ".out"), "w", encoding="latin-1") as f:
            f.write(out)
        with open(os.path.join(EXPECTED, cid + ".rc"), "w") as f:
            f.write(str(proc.returncode) + "\n")
        print(f"[host] {cid}: rc={proc.returncode} bytes={len(out)}")
    print(f"\nwrote {len(cases)} expected outputs to {EXPECTED}")


def cmd_gencmds(args):
    for cid, tokens in load_cases(pilot_only=args.pilot):
        print(pi_command(tokens))


def parse_log(log_path, cases):
    """Region-based parse keyed on each case's full echoed command string.

    Split the log into lines (strip \\r). For each case, find the FIRST line that
    contains its exact Pi command string -- that is psh's echo of the typed line.
    A case's output region runs from just after its echo line up to the echo line
    of the NEXT case (in send order), with all (psh)% prompt/rescue-empty lines
    dropped. This is robust to the psh-interact rescue newline (extra empty prompt)
    and to cold-exec output that arrives after the idle timer fired.
    """
    with open(log_path, "rb") as f:
        raw = f.read().decode("latin-1")
    lines = [ANSI_CSI.sub("", ln.replace("\r", "")) for ln in raw.split("\n")]

    # locate each case's echo line index
    idx = {}
    for cid, tokens in cases:
        pic = pi_command(tokens)
        found = None
        for i, ln in enumerate(lines):
            if pic in ln:
                found = i
                break
        idx[cid] = found

    order = [cid for cid, _ in cases]
    results = {}
    for pos, cid in enumerate(order):
        start = idx[cid]
        if start is None:
            results[cid] = None  # command never echoed -> not captured
            continue
        # end = next case (in send order) that WAS found
        end = len(lines)
        for later in order[pos + 1:]:
            if idx[later] is not None and idx[later] > start:
                end = idx[later]
                break
        region = lines[start + 1:end]
        body = [ln for ln in region if not ln.lstrip().startswith(PSH_PROMPT)]
        # drop a single trailing empty line artifact from capture framing, keep internal ones
        while body and body[-1] == "":
            body.pop()
        results[cid] = "\n".join(body) + ("\n" if body else "")
    return results


def cmd_check(args):
    os.makedirs(PIOUT, exist_ok=True)
    cases = load_cases(pilot_only=args.pilot)
    piout = parse_log(args.log, cases)

    rows = []
    n_pass = n_fail = n_nocap = 0
    for cid, tokens in cases:
        exp_path = os.path.join(EXPECTED, cid + ".out")
        if not os.path.exists(exp_path):
            rows.append((cid, "NO-EXPECTED", tokens, None, None))
            continue
        with open(exp_path, encoding="latin-1") as f:
            expected = f.read()
        got = piout.get(cid)
        if got is not None:
            got = normalize(got, is_pi=True)  # rewrite /root/cutest/ away to match host bare names
        # persist the parsed Pi output for auditing
        with open(os.path.join(PIOUT, cid + ".out"), "w", encoding="latin-1") as f:
            f.write("" if got is None else got)
        if got is None:
            n_nocap += 1
            rows.append((cid, "NOT-CAPTURED", tokens, expected, None))
        elif got == expected:
            n_pass += 1
            rows.append((cid, "PASS", tokens, expected, got))
        else:
            n_fail += 1
            rows.append((cid, "FAIL", tokens, expected, got))

    # console table
    print(f"\n{'CASE':<14} {'RESULT':<13} COMMAND")
    print("-" * 70)
    for cid, res, tokens, _, _ in rows:
        print(f"{cid:<14} {res:<13} {pi_command(tokens)}")
    print("-" * 70)
    print(f"PASS={n_pass}  FAIL={n_fail}  NOT-CAPTURED={n_nocap}  total={len(rows)}")

    # write RESULTS.md
    write_results_md(rows, n_pass, n_fail, n_nocap, args)
    return 0 if n_fail == 0 and n_nocap == 0 else 1


def _fmt_block(text):
    if text is None:
        return "(none)"
    return text if text != "" else "(empty)"


def write_results_md(rows, n_pass, n_fail, n_nocap, args):
    md = [os.path.join(HERE, "RESULTS.md")]
    lines = []
    lines.append("# coreutils-difftest results\n")
    lines.append(
        "Differential-correctness of the ported **GNU coreutils 9.5** `/usr/bin/<tool>` "
        "binaries on Phoenix-RTOS (real Pi4, netboot) vs the **same GNU coreutils 9.5 built "
        "natively on the host** (reference), over a curated deterministic corpus.\n")
    lines.append(f"- reference: native GNU coreutils 9.5 (`--refdir {args.__dict__.get('refdir','?')}`)\n")
    lines.append(f"- log: `{args.log}`\n")
    lines.append(f"- **PASS={n_pass}  FAIL={n_fail}  NOT-CAPTURED={n_nocap}  total={len(rows)}**\n")
    lines.append("\n## Summary table\n")
    lines.append("| case | result | command |")
    lines.append("|------|--------|---------|")
    for cid, res, tokens, _, _ in rows:
        lines.append(f"| `{cid}` | {res} | `{pi_command(tokens)}` |")
    fails = [r for r in rows if r[1] in ("FAIL", "NOT-CAPTURED")]
    if fails:
        lines.append("\n## FAIL / NOT-CAPTURED detail\n")
        for cid, res, tokens, expected, got in fails:
            lines.append(f"### `{cid}` — {res}\n")
            lines.append(f"command: `{pi_command(tokens)}`\n")
            lines.append("host (GNU 9.5) expected:\n")
            lines.append("```\n" + _fmt_block(expected) + "```\n")
            lines.append("Pi (Phoenix) got:\n")
            lines.append("```\n" + _fmt_block(got) + "```\n")
    with open(md[0], "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print(f"wrote {md[0]}")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("host")
    p.add_argument("--refdir", required=True, help="dir with native GNU-9.5 binaries (…/coreutils-9.5/src)")
    p.set_defaults(func=cmd_host)

    p = sub.add_parser("gencmds")
    p.add_argument("--pilot", action="store_true")
    p.set_defaults(func=cmd_gencmds)

    p = sub.add_parser("check")
    p.add_argument("--log", required=True)
    p.add_argument("--pilot", action="store_true")
    p.add_argument("--refdir", default="(native GNU 9.5)")
    p.set_defaults(func=cmd_check)

    args = ap.parse_args()
    sys.exit(args.func(args) or 0)


if __name__ == "__main__":
    main()
