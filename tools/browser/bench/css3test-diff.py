#!/usr/bin/env python3
#
# css3test-diff.py -- compare two css3test runs feature by feature (Phoenix-RTOS browser
# benchmark suite; docs/browser/CSS3TEST-GAPS.md).
#
#     tools/browser/bench/css3test-diff.py A-final.json B-final.json
#
# The inputs are the final JSONs the css3test hook POSTs (hooks/css3test.html): a Pi run
# (artifacts/browser-bench/pi-results/) or a host run (run-host-baseline.sh). Runs made with the
# hook's "failures" list are compared per feature: every feature whose pass fraction differs, the
# test-count difference (B - A) and the tests that failed on one side only. Older runs, without
# the list, are compared per specification (css3test's rounded percentages) only.
#
# SPDX-License-Identifier: BSD-3-Clause

import json
import sys


def load(path):
    with open(path) as f:
        return json.load(f)


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: css3test-diff.py A-final.json B-final.json")
    a, b = load(sys.argv[1]), load(sys.argv[2])
    for tag, run, path in (("A", a, sys.argv[1]), ("B", b, sys.argv[2])):
        print(f"{tag}: {run.get('score')} passed={run.get('passed')}/{run.get('total')} "
              f"run={run.get('run')} ua={run.get('userAgent', '?')[:90]}  ({path})")
    if a.get("total") != b.get("total"):
        print("warning: different test totals -- not the same css3test or filter")

    specs_a = {s[0]: s[2] for s in a.get("specs", [])}
    specs_b = {s[0]: s[2] for s in b.get("specs", [])}
    print("\nspecifications that differ (A -> B):")
    for spec in sorted(set(specs_a) | set(specs_b)):
        if specs_a.get(spec) != specs_b.get(spec):
            print(f"  {spec:32s} {specs_a.get(spec, '-'):>5s} -> {specs_b.get(spec, '-'):>5s}")

    if "failures" not in a or "failures" not in b:
        print("\n(no per-feature detail: a run predates the hook's \"failures\" list)")
        return

    # [spec, group, feature, percent, tests, [failed tests]]; a feature absent passed fully
    fail_a = {tuple(f[:3]): f for f in a["failures"]}
    fail_b = {tuple(f[:3]): f for f in b["failures"]}
    rows = []
    for key in sorted(set(fail_a) | set(fail_b)):
        fa, fb = fail_a.get(key), fail_b.get(key)
        pa = fa[3] if fa else 100.0
        pb = fb[3] if fb else 100.0
        if pa == pb:
            continue
        tests = (fa or fb)[4]
        only_a = sorted(set(fa[5] if fa else []) - set(fb[5] if fb else []))
        only_b = sorted(set(fb[5] if fb else []) - set(fa[5] if fa else []))
        rows.append((key, tests, pa, pb, (pb - pa) * tests / 100.0, only_a, only_b))

    total = sum(r[4] for r in rows)
    print(f"\nfeatures that differ: {len(rows)}, net tests B - A = {total:+.1f}")
    for (spec, group, feature), tests, pa, pb, delta, only_a, only_b in rows:
        print(f"  {delta:+6.1f}  {spec} / {group} / {feature}  ({tests} tests) {pa:g}% -> {pb:g}%")
        for t in only_a:
            print(f"           fails only in A: {t}")
        for t in only_b:
            print(f"           fails only in B: {t}")


if __name__ == "__main__":
    main()
