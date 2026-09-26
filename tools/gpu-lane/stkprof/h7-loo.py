#!/usr/bin/env python3
"""Leave-one-out pixel guard for the H7 quakespasm arms (replaces the 95 % rule of h7-pxcompare.py).

The fixed 95 % threshold of h7-pxcompare.py sits INSIDE base-to-base noise (a third base run
scored 93.4 %: the hashes cross the UART, which corrupts ~1.3 % of lines, and some frames are not
deterministic). This script calibrates against the bases themselves: each base run is held out
and compared with the frames the other two agree on; each arm is compared with every base pair.
An arm is clean when its mismatch rate and longest mismatch run sit inside the held-out bases'.

Usage: h7-loo.py <label-prefix-of-bases> <arm-label> [<arm-label> ...]
   e.g. h7-loo.py h7-k0x100 h7-k0x1af-1 h7-k0x1ef-1   (logs artifacts/rpi4b-uart/*-<label>.log;
        bases are <prefix>-1, -2, -3)
Copyright 2026 Phoenix Systems
SPDX-License-Identifier: BSD-3-Clause
"""
import glob
import importlib.util
import os
import sys

here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("pxc", os.path.join(here, "h7-pxcompare.py"))
pxc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pxc)


def log(label):
    return pxc.load(glob.glob(f"artifacts/rpi4b-uart/*-{label}.log")[0])


def test(ref_pair, x):
    r, s = ref_pair
    d0 = pxc.align(r, s, 400)[0]
    da = pxc.align(r, x, 400)[0]
    det = [n for n in r if (n + d0) in s and r[n][0] == s[n + d0][0] and r.get(n - 1, ("",))[0] != r[n][0]]
    on = [n for n in det if (n + da) in x]
    miss = {n for n in on if x[n + da][0] != r[n][0]}
    run = worst = 0
    for n in sorted(on):
        run = run + 1 if n in miss else 0
        worst = max(worst, run)
    black = sum(1 for n in on if x[n + da][1] == 256 and r[n][1] != 256)
    return 100.0 * len(miss) / len(on), worst, black


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    bases = [log(f"{sys.argv[1]}-{i}") for i in (1, 2, 3)]
    pairs = ((0, 1), (0, 2), (1, 2))
    print("held-out base vs the other two:")
    for k in range(3):
        m, w, b = test([bases[i] for i in range(3) if i != k], bases[k])
        print(f"  base{k + 1}: miss {m:.1f} %  longest_run {w}  new_black {b}")
    print("arms, mean over the three base pairs:")
    for a in sys.argv[2:]:
        res = [test((bases[i], bases[j]), log(a)) for i, j in pairs]
        print(f"  {a}: miss {sum(t[0] for t in res) / 3:.1f} %  longest_run {sum(t[1] for t in res) / 3:.1f}  "
              f"new_black {max(t[2] for t in res)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
