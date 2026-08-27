#!/usr/bin/env python3
# STK host-vs-Pi visual-parity comparison (owner request 2026-08-27).
#
# The Pi is the device under test; the host (AMD GPU / Ubuntu) is the reference.
# For each named scene we load the Pi frame + the host frame, align sizes, and
# report SSIM (grayscale structural similarity, 0..1) + MAE (mean abs RGB error)
# + a side-by-side composite PNG for visual judgment.
#
# NOTE (vs the Quake harness): STK is NOT frame-deterministic like the Quake
# replay, so the menu/loading are near-deterministic (SSIM meaningful) while the
# race-grid is a *roughly*-corresponding countdown frame — the side-by-side is
# the primary judgment, SSIM is a supporting indicator (small camera/timing
# differences depress SSIM without meaning the render is wrong).
#
# Run with the venv python (has scikit-image): .venv/bin/python scripts/stk-visual-compare.py
# SPDX-License-Identifier: BSD-3-Clause

import os, sys
import numpy as np
from PIL import Image
from skimage.metrics import structural_similarity as ssim

BASE = os.path.join(os.path.dirname(__file__), "..", "artifacts", "stk-compare")
PI, HOST, OUT = (os.path.join(BASE, d) for d in ("pi", "host", "sidebyside"))
SCENES = ["menu", "loading", "olivermath-grid"]


def load(path):
    return Image.open(path).convert("RGB")


def to_gray_arr(img):
    return np.asarray(img.convert("L"))


def main():
    os.makedirs(OUT, exist_ok=True)
    rows = []
    for scene in SCENES:
        pi_p = os.path.join(PI, scene + ".png")
        host_p = os.path.join(HOST, scene + ".png")
        if not (os.path.exists(pi_p) and os.path.exists(host_p)):
            rows.append((scene, "MISSING", "-", "-",
                         f"pi={os.path.exists(pi_p)} host={os.path.exists(host_p)}"))
            continue
        pi_img, host_img = load(pi_p), load(host_p)
        # Align the host frame to the Pi frame's dimensions (both should be 1080p;
        # resize host if not, so SSIM/MAE are computed on matching grids).
        if host_img.size != pi_img.size:
            host_img = host_img.resize(pi_img.size, Image.LANCZOS)
        pa, ha = np.asarray(pi_img), np.asarray(host_img)
        s = float(ssim(to_gray_arr(pi_img), to_gray_arr(host_img), data_range=255))
        mae = float(np.abs(pa.astype(int) - ha.astype(int)).mean())
        # Side-by-side composite: Pi (left) | host (right), a 8px white gutter.
        w, h = pi_img.size
        comp = Image.new("RGB", (w * 2 + 8, h), (255, 255, 255))
        comp.paste(pi_img, (0, 0))
        comp.paste(host_img, (w + 8, 0))
        comp_p = os.path.join(OUT, scene + "-pi-vs-host.png")
        comp.save(comp_p)
        rows.append((scene, f"{s:.3f}", f"{mae:.1f}", os.path.basename(comp_p), "ok"))

    print("scene                 ssim    mae    sidebyside                         note")
    print("-" * 92)
    for scene, s, mae, sbs, note in rows:
        print(f"{scene:<20} {s:>6}  {mae:>5}   {sbs:<34} {note}")
    print("\nssim: 1.0=identical (grayscale structure). mae: 0=identical (0..255 RGB).")
    print("Side-by-sides in artifacts/stk-compare/sidebyside/ (Pi left | host right).")
    print("Reminder: race-grid SSIM is INDICATIVE only (STK not frame-deterministic);")
    print("judge the render parity from the side-by-side image.")


if __name__ == "__main__":
    sys.exit(main())
