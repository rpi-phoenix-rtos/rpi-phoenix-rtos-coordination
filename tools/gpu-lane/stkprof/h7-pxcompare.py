#!/usr/bin/env python3
"""H7 correctness guard: compare the per-frame scanout grid hashes of two runs.

`rpi4-v3d-async -k <knobs|0x100>` (V3DA_KNOB_PX_LOG) prints, for every presented frame,
    V3DA srv pxlog n=<pan index> h=<fnv1a32 of a 16x16 pixel grid> zero=<black samples>
QuakeSpasm's `+timedemo demo1` renders the same demo frames in the same order on every run,
so two correct runs produce (mostly) the same hash sequence, shifted by however many pans
the console/loading screens took. This script finds that shift and reports the match rate.

Pre-registered use (docs/gpu-new-lane/E2b-v3d-render-slowness.md, H7) -- THREE logs:
    h7-pxcompare.py <base1> <base2> <arm>
The frames on which the two base runs agree are the deterministic subset (particles, water
warp and animated textures make some frames differ run to run, so R0 = base-vs-base can be
well below 100 %). The arm passes when it is identical to base1 on >= 95 % of that subset and
adds no black frames. ("R0 - 5 points" alone would pass a run that mangles every third frame
when R0 is ~60 %.) A tile/geometry corruption changes the grid hash of the frames it hits.

Usage: h7-pxcompare.py <log A> <log B> [<arm log>] [--max-shift N]   (default 400 pans)
Copyright 2026 Phoenix Systems
SPDX-License-Identifier: BSD-3-Clause
"""
import re
import sys

RE = re.compile(r"V3DA srv pxlog n=(\d+) h=([0-9a-f]{8}) zero=(\d+)\s*$")


def load(path):
    seq = {}
    with open(path, "rb") as f:
        for raw in f:
            m = RE.search(raw.decode("utf-8", "replace").rstrip("\r\n"))
            if m:
                seq[int(m.group(1))] = (m.group(2), int(m.group(3)))
    return seq


def main():
    args = sys.argv[1:]
    shift_max = 400
    if "--max-shift" in args:
        i = args.index("--max-shift")
        shift_max = int(args[i + 1])
        del args[i:i + 2]
    if len(args) not in (2, 3):
        print(__doc__)
        return 2
    if len(args) == 3:
        return three(args, shift_max)
    a, b = load(args[0]), load(args[1])
    print(f"A: {args[0]}: {len(a)} frames, black {sum(1 for v in a.values() if v[1] == 256)}")
    print(f"B: {args[1]}: {len(b)} frames, black {sum(1 for v in b.values() if v[1] == 256)}")
    if not a or not b:
        print("no pxlog lines in one of the logs (was the server started with V3DA_KNOB_PX_LOG = 0x100?)")
        return 1
    best = align(a, b, shift_max)
    if best is None:
        print("no alignment with >= 50 common frames")
        return 1
    d, hit, n = best
    worst, run = 0, 0
    for k in sorted(k for k in a if (k + d) in b):
        run = run + 1 if a[k][0] != b[k + d][0] else 0
        worst = max(worst, run)
    print(f"best shift B = A {d:+d} pans: {hit}/{n} frames identical = {100.0 * hit / n:.1f} %; "
          f"longest mismatch run {worst} frames")
    return 0


def align(a, b, shift_max):
    best = None
    for d in range(-shift_max, shift_max + 1):
        common = [n for n in a if (n + d) in b]
        if len(common) < 50:
            continue
        hit = sum(1 for n in common if a[n][0] == b[n + d][0])
        if best is None or hit > best[1]:
            best = (d, hit, len(common))
    return best


def three(paths, shift_max):
    b1, b2, arm = (load(p) for p in paths)
    for name, seq in zip(("base1", "base2", "arm"), (b1, b2, arm)):
        print(f"{name}: {len(seq)} frames, black {sum(1 for v in seq.values() if v[1] == 256)}")
    r0 = align(b1, b2, shift_max)
    ra = align(b1, arm, shift_max)
    if r0 is None or ra is None:
        print("no alignment with >= 50 common frames -- no verdict")
        return 1
    d0, da = r0[0], ra[0]
    det = [n for n in b1 if (n + d0) in b2 and b1[n][0] == b2[n + d0][0]]
    # drop static frames (a hash repeated in consecutive frames: console/loading) from the subset
    det = [n for n in det if b1.get(n - 1, ("",))[0] != b1[n][0]]
    on = [n for n in det if (n + da) in arm]
    same = sum(1 for n in on if arm[n + da][0] == b1[n][0])
    black_new = sum(1 for n in on if arm[n + da][1] == 256 and b1[n][1] != 256)
    rate = 100.0 * same / len(on) if on else float("nan")
    print(f"R0 (base1 vs base2): {r0[1]}/{r0[2]} = {100.0 * r0[1] / r0[2]:.1f} %; deterministic changing frames: {len(det)}")
    print(f"arm vs base1 on that subset: {same}/{len(on)} = {rate:.1f} % identical; new black frames {black_new}")
    ok = on and rate >= 95.0 and black_new == 0 and len(on) >= 100
    print("GUARD " + ("PASS" if ok else "FAIL (or too few deterministic frames: need >= 100)"))
    return 0 if ok else 3


if __name__ == "__main__":
    sys.exit(main())
