#!/usr/bin/env python3
"""stk-fps-from-hdmi.py -- read SuperTuxKart's FPS HUD out of the HDMI snapshots.

Why this exists: STK's frame rate is on the SCREEN, not in the UART log. Its
`show_fps` HUD prints "FPS: min/avg/max - N KTris" in the top-right of the
render, and nothing about it reaches the serial console. A bench that greps the
UART log for a frame rate therefore finds NOTHING and looks like a failed run
when the trials actually rendered fine -- that happened to the 8-trial
`stkrate2` bench on 2026-09-09, whose numbers were sitting in
artifacts/hdmi/ the whole time.

It also should not be a single screenshot. Per-trial variation is real (one of
eight trials read 7/7/9 where the rest read 8/9/9), so quote a rate across
trials, never one frame -- see docs/misc/2026-09-09-stk-fps-scale-rtts.md.

This picks, for each trial of a labelled bench, the LARGEST snapshot, crops the
HUD, and stacks the crops into one contact sheet to read in a single glance.

Largest works HERE because an in-race STK frame is dense 3D, but do not
generalise it: PNG size tracks detail, not "did it render". A dense text screen
beats a flat one. The Pi firmware's red netboot screen (~333 kB) outweighs a
rendered Window Maker desktop (~178 kB), so picking the biggest frame of an X
session hands you the *boot screen* and makes a working desktop look dead. That
cost a wrong "did not render" call on 2026-09-10. For anything that is not a
dense 3D scene, look at the LAST frame.

Usage:
  ./scripts/stk-fps-from-hdmi.py <label> [-o out.png] [--trials 8]

  <label> matches the bench label, e.g. `stkrate2` for
  artifacts/hdmi/*-stkrate2-T<N>-*.png. A trial with no snapshot, or whose
  frames are all tiny, is reported as MISSING rather than skipped silently.

Copyright 2026 Phoenix Systems
SPDX-License-Identifier: BSD-3-Clause
"""
import argparse
import glob
import os
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("needs Pillow: uv venv .venv && .venv/bin/python -m pip install pillow")

# The HUD sits in the top-right of STK's 1920x1080 render. These bounds were
# read off a real capture; they are deliberately generous so a slightly
# different string length still fits.
HUD_BOX = (620, 0, 1000, 42)
SCALE = 3
# Below this a PNG is a black screen or a text console, not a rendered scene.
MIN_RENDERED_BYTES = 500 * 1024


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("label")
    ap.add_argument("-o", "--out", default=None)
    ap.add_argument("--trials", type=int, default=8)
    ap.add_argument("--hdmi-dir", default="artifacts/hdmi")
    args = ap.parse_args()

    out = args.out or f"artifacts/hdmi/{args.label}-fps-hud.png"
    tiles, missing = [], []

    for trial in range(1, args.trials + 1):
        pat = os.path.join(args.hdmi_dir, f"*-{args.label}-T{trial}-*.png")
        frames = sorted(glob.glob(pat), key=os.path.getsize)
        if not frames:
            missing.append((trial, "no snapshots"))
            continue
        best = frames[-1]
        size = os.path.getsize(best)
        if size < MIN_RENDERED_BYTES:
            missing.append((trial, f"largest frame only {size // 1024} kB -- never rendered"))
            continue
        img = Image.open(best).convert("RGB")
        crop = img.crop(HUD_BOX)
        w, h = crop.size
        tiles.append((trial, os.path.basename(best),
                      crop.resize((w * SCALE, h * SCALE), Image.LANCZOS)))

    if not tiles:
        print(f"no rendered trials found for label '{args.label}'", file=sys.stderr)
        for trial, why in missing:
            print(f"  T{trial}: {why}", file=sys.stderr)
        return 1

    tw, th = tiles[0][2].size
    sheet = Image.new("RGB", (tw, th * len(tiles)), (20, 20, 20))
    for i, (_, _, crop) in enumerate(tiles):
        sheet.paste(crop, (0, i * th))
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    sheet.save(out)

    print(f"contact sheet: {out}")
    print(f"rows, top to bottom ({len(tiles)} rendered trials):")
    for trial, name, _ in tiles:
        print(f"  T{trial}  {name}")
    for trial, why in missing:
        print(f"  T{trial}  MISSING -- {why}")
    print("\nRead the FPS min/avg/max off each row. Quote the spread across trials,")
    print("not a single row.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
