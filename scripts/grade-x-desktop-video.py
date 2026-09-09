#!/usr/bin/env python3
"""grade-x-desktop-video.py <video.mp4> [--fps 3] — grade an X11 desktop recording.

Answers, per frame, the three things the owner actually reported on 2026-09-09:

  1. a mirrored Window Maker Clip in the BOTTOM-LEFT corner that is a copy of the
     top-left corner;
  2. white/grey bands appearing in the Game-of-Life xterm that look copied from the
     `top` xterm;
  3. xbill hidden behind another window, "so you can't tell what is happening".

Why this exists as a script rather than an ad-hoc command: all three detectors were
written ad-hoc, and EVERY one of them returned a wrong verdict at least once before
it discriminated (a bad icon-band count reported FAIL on a correct frame; the grey
detector first counted JPEG/anti-alias halo; its successor then counted the xterm's
own status bar; a seam check ran on an ROI the layout had moved out from under). A
detector that lives only in shell history gets re-derived, and re-derived wrong.

Needs numpy + Pillow, which live in the repo venv only (the host python is
PEP 668-managed and must not be pip-installed into), so run it as
`.venv/bin/python scripts/grade-x-desktop-video.py <video.mp4>`.

Copyright 2026 Phoenix Systems
SPDX-License-Identifier: BSD-3-Clause
"""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

REPO = Path(__file__).resolve().parent.parent
LAUNCHER = REPO / "tools/x11-port/launcher/pl_phoenix_xlaunch.c"

# Window Maker's titlebar. -geometry places the CLIENT area, so the frame sits
# above the requested y; the ROIs below are the client rects, offset by this.
WM_TITLEBAR = 24

# Thresholds, each with the measurement that fixed it:
MIRROR_MAD_OK = 40.0   # broken = 0.00 (exact mirror) .. 6.85; fixed = 71..77
GREY_RUN_OK = 60       # clean frames peak at 5 px (glyph AA); contaminated = 516 px
XBILL_LIGHT_OK = 0.70  # visible+unobstructed measured 0.86..0.88


def launcher_geom(name):
    """Read one -geometry string out of the launcher's `action` layout.

    Parsed from source, not hardcoded, precisely because the layout moves: the
    xbill fix relocated the Game-of-Life xterm into a strip that used to be bare
    desktop, which is how the seam check ended up aimed at nothing.
    """
    src = LAUNCHER.read_text()
    m = re.search(r"\*const\s+%s\[\d+\]\s*=\s*\{\s*\"-geometry\",\s*\"([0-9x+]+)\"" % re.escape(name), src)
    if not m:
        sys.exit("cannot find %s geometry in %s" % (name, LAUNCHER))
    g = re.match(r"(\d+)x(\d+)\+(\d+)\+(\d+)$", m.group(1))
    if not g:
        sys.exit("%s geometry %r is not WxH+X+Y" % (name, m.group(1)))
    return tuple(int(v) for v in g.groups())  # w_chars_or_px, h, x, y


def char_cell_px(w_chars, h_chars):
    """xterm geometry is in CHARACTERS, and we do not know the shipped font's
    metrics. This returns a deliberately OVER-large search box; the exact cell
    area is then measured per frame by cell_area() below."""
    return w_chars * 7, h_chars * 15


def cell_area(img, search):
    """Locate the xterm's black cell area inside `search`, per frame.

    Contiguity is the whole point. Two weaker versions failed first:

      * the rect guessed from char metrics (7 px/char) overshot the real 6 px
        font, so the box ran 96 px past the window's right edge and the detector
        fired on 12/300 frames of a known-GOOD recording -- all hits on the box's
        first row, which was Window Maker's titlebar;
      * taking the bounding box of every majority-dark row/column then absorbed
        the DARK PARTS OF THE NEIGHBOURING WINDOWS (the GL window's render and the
        desktop sit immediately to the right), so a pale shape in the GL window
        read as contamination inside the xterm. That would have reported the
        owner's artefact as still present on a clean build.

    So: find the window's top-left dark corner, then walk right and down while the
    region stays majority dark, stopping at the first sustained break. A different
    window on the other side of a border cannot be reached. Returns None when no
    such region exists -- reported as its own outcome, never as a pass.
    """
    x, y, w, h = search
    sub = img[y:y + h, x:x + w, :]
    if sub.size == 0:
        return None
    dark = sub.max(axis=2) < 60

    # top edge: first row that is majority dark across the left half of the box
    left_probe = max(8, (w // 4))
    rows = np.where(dark[:, :left_probe].mean(axis=1) > 0.6)[0]
    if rows.size == 0:
        return None
    r0 = int(rows[0])

    # right edge: walk right along a band certain to be inside the window,
    # stopping after 3 consecutive columns that are not majority dark.
    band = dark[r0:r0 + min(120, h - r0), :]
    if band.shape[0] < 16:
        return None
    c1, gap = 0, 0
    for c in range(band.shape[1]):
        if band[:, c].mean() > 0.5:
            c1, gap = c, 0
        else:
            gap += 1
            if gap >= 3:
                break

    # bottom edge: same walk downward, over the column range just established.
    colslice = dark[:, :c1 + 1]
    r1, gap = r0, 0
    for r in range(r0, colslice.shape[0]):
        if colslice[r].mean() > 0.5:
            r1, gap = r, 0
        else:
            gap += 1
            if gap >= 3:
                break

    if c1 < 32 or (r1 - r0) < 32:
        return None
    return (x + 2, y + r0 + 2, c1 - 3, r1 - r0 - 3)


def mirror_mad(img):
    """MAD of the 64x64 bottom-left tile against a vflip of the top-left tile.

    A mirror artefact makes these identical (MAD -> 0). Compared over a small dy
    sweep so a one-off row offset does not read as "fixed"."""
    # NB: BOTH axes must be sliced. `img[:, :64]` is a 1080-row column strip, which
    # never matches the 64-row bottom tile, so every comparison was skipped and the
    # function returned NaN -- and `NaN < threshold` is False, so the check reported
    # a clean PASS on a video with 377 mirrored frames. Sixth detector error in this
    # project, and the first that failed OPEN rather than closed.
    a = img[:64, :64, :].astype(np.int16)                # top-left tile, y 0..63
    best = None
    for dy in (-2, 0, 2):
        y0 = img.shape[0] - 64 + dy
        b = img[y0:y0 + 64, :64, :].astype(np.int16)
        if b.shape != a.shape:
            continue
        mad = float(np.abs(np.flipud(b) - a).mean())
        best = mad if best is None else min(best, mad)
    return best if best is not None else float("nan")


def longest_light_run(img, box):
    """Longest contiguous horizontal run of achromatic LIGHT pixels inside `box`,
    ignoring rows that are mostly light.

    Two corrections are baked in. (1) The metric is a contiguous RUN, not a pixel
    count: the Game-of-Life cells are white glyphs on black, so counting light
    pixels fires on a perfectly clean frame (glyph + JPEG halo), and localising
    them was what showed they were <=16 px scattered per row rather than blocks.
    (2) Rows that are >60% light are SKIPPED, because the xterm's own status line
    is a full-width light bar and a fixed sub-rect excluding it goes stale the
    moment the layout moves -- which is exactly how the second version of this
    detector reported a defect on a clean build.
    """
    x, y, w, h = box
    roi = img[y:y + h, x:x + w, :].astype(np.int16)
    if roi.size == 0:
        return 0
    mn = roi.min(axis=2)
    mx = roi.max(axis=2)
    light = (mn > 110) & ((mx - mn) < 40)
    worst = 0
    for row in light:
        if row.mean() > 0.60:  # the status bar, not contamination
            continue
        run = 0
        for v in row:
            run = run + 1 if v else 0
            if run > worst:
                worst = run
    return worst


def light_fraction(img, box):
    x, y, w, h = box
    roi = img[y:y + h, x:x + w, :].astype(np.int16)
    if roi.size == 0:
        return 0.0
    mn = roi.min(axis=2)
    return float((mn > 140).mean())


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    video = Path(sys.argv[1])
    fps = 3
    if "--fps" in sys.argv:
        fps = int(sys.argv[sys.argv.index("--fps") + 1])

    lw, lh, lx, ly = launcher_geom("term_life")
    lpx, lpy = char_cell_px(lw, lh)
    gol_box = (lx, ly + WM_TITLEBAR, lpx, lpy)

    bw, bh, bx, by = launcher_geom("bill_geom")
    bill_box = (bx, by + WM_TITLEBAR, bw, bh)

    print("layout from %s:" % LAUNCHER.name)
    print("  GoL xterm  box %s" % (gol_box,))
    print("  xbill      box %s" % (bill_box,))

    with tempfile.TemporaryDirectory() as td:
        subprocess.run(["ffmpeg", "-v", "error", "-i", str(video), "-vf", "fps=%d" % fps,
                        "%s/f%%05d.png" % td], check=True)
        frames = sorted(Path(td).glob("f*.png"))
        if not frames:
            sys.exit("no frames extracted from %s" % video)

        mirror_bad = grey_bad = bill_bad = blank = notfound = gol_graded = 0
        worst_mad, worst_run, worst_bill = None, 0, 1.0
        for f in frames:
            img = np.asarray(Image.open(f).convert("RGB"))
            if img.mean() < 8:  # power-off / pre-desktop black
                blank += 1
                continue
            mad = mirror_mad(img)
            bl = light_fraction(img, bill_box)

            # The GoL check is the only layout-DEPENDENT one: its ROI comes from
            # the current launcher geometry, so an older recording made under a
            # different layout has no window there. That must disable only this
            # detector -- an earlier version `continue`d the whole frame, which
            # dropped the mirror sample on the owner's own video from 326/442 to
            # 1/39 and nearly turned a real artefact into a rounding error.
            cells = cell_area(img, gol_box)
            if cells is None:
                notfound += 1
                run = None
            else:
                gol_graded += 1
                run = longest_light_run(img, cells)
            if mad != mad:  # NaN: the detector did not run, which is NOT a pass
                sys.exit("FAIL mirror detector produced NaN on %s -- fix the detector" % f.name)
            worst_mad = mad if worst_mad is None else min(worst_mad, mad)
            worst_bill = min(worst_bill, bl)
            mirror_bad += mad < MIRROR_MAD_OK
            bill_bad += bl < XBILL_LIGHT_OK
            if run is not None:
                worst_run = max(worst_run, run)
                grey_bad += run > GREY_RUN_OK

        graded = len(frames) - blank
        print("\nframes: %d total, %d graded (%d blank/pre-desktop, %d with no GoL window found)"
              % (len(frames), graded, blank, notfound))
        if graded == 0:
            sys.exit("FAIL no frame was gradeable -- the recording started before the desktop was up")
        if gol_graded == 0:
            print("  WARN the GoL xterm was not found in ANY frame -- its ROI comes from the\n"
                  "       CURRENT launcher layout, so this recording predates it. That check\n"
                  "       is UNGRADED below, not passed.")
        elif notfound:
            print("  note GoL window found in %d of %d frames" % (gol_graded, graded))
        rc = 0
        for label, bad, worst, fmt in (
                ("mirrored Clip bottom-left", mirror_bad, worst_mad, "worst MAD %.2f (need >= %.0f)" % (worst_mad, MIRROR_MAD_OK)),
                ("GoL grey/white bands", grey_bad, worst_run,
                 ("longest light run %d px of %d frames (need <= %d)" % (worst_run, gol_graded, GREY_RUN_OK))
                 if gol_graded else "UNGRADED: window not found in any frame"),
                ("xbill obscured", bill_bad, worst_bill, "min light fraction %.2f (need >= %.2f)" % (worst_bill, XBILL_LIGHT_OK))):
            if label.startswith("GoL") and gol_graded == 0:
                print("  %s %-28s %s" % ("????", label, "UNGRADED -- see note above"))
                rc = 1
                continue
            tag = "OK  " if bad == 0 else "FAIL"
            if bad:
                rc = 1
            print("  %s %-28s %4d/%d frames affected | %s" % (tag, label, bad, graded, fmt))
        print("\nRESULT: %s" % ("desktop CLEAN on all three reported artefacts" if rc == 0
                                else "artefact PRESENT -- do not publish this recording"))
        return rc


if __name__ == "__main__":
    sys.exit(main())
