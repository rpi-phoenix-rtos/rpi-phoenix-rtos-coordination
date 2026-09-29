#!/usr/bin/env python3
"""check-torch-rois.py - fail a Pi cycle when the Quake start-map torches are absent.

Why this exists
---------------
The vkQuake wall-torch bug was declared FIXED on 2026-08-22 on the strength of an
HDMI grab that actually showed a `misc_fireball` LAVABALL -- a moving projectile
on a periodic timer -- and not a wall torch. The archway torches were absent in
that frame and stayed absent for 12 days until the owner re-reported them. The
defect was never the renderer alone: it was that "looks fixed to a human glancing
at a screenshot" was the only instrument we had.

So this is the instrument. It scores the two archway torch ROIs from
docs/misc/torch-archaeology/start-map-torch-rois.json and exits non-zero when
they are dark, and it EXPLICITLY ignores the lavaball ROI that caused the false
positive.

Usage
-----
    ./scripts/check-torch-rois.py <frame.png> [more.png ...]
    ./scripts/check-torch-rois.py --label final-qs        # score a cycle's ticks
    ./scripts/check-torch-rois.py --spec <other.json> ...

Exit 0 only when at least --min-pass frames (default 2) have BOTH required ROIs
lit. Two frames, because the flame animates and a single grab can tear or catch
it mid-cycle -- one frame is not evidence either way.

Copyright 2026 Phoenix Systems
SPDX-License-Identifier: BSD-3-Clause
"""

import argparse
import glob

import json
import re
import os
import sys


def hdmi_glob(top, name_pat):
    """artifacts/hdmi/<name_pat> plus the month archive folders (YYYY-MM/,
    scripts/archive-hdmi-snapshots.py), in capture order: every name starts with its timestamp."""
    pats = [name_pat]
    if name_pat.endswith(".png"):  # archived months are WebP (scripts/compress-hdmi-archive.py)
        pats.append(name_pat[:-4] + ".webp")
    files = set(glob.glob(os.path.join(top, name_pat)))
    for p in pats:
        files.update(glob.glob(os.path.join(top, "20[0-9][0-9]-[0-9][0-9]", p)))
    return sorted(files, key=os.path.basename)


try:
    from PIL import Image, ImageChops
except ImportError:
    sys.exit("check-torch-rois: needs Pillow (PIL) - not importable")

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_SPEC = os.path.join(REPO, "docs", "misc", "torch-archaeology",
                            "start-map-torch-rois.json")


def flame_pixels(img, box, basis_w):
    """Count flame-fullbright-core pixels in box, per the spec's pass rule.

    R>100 && R>G+20 && R>B+20: a warm-biased pixel brighter than the surrounding
    stone. CALIBRATED, not assumed -- the spec's original rule (R>180, an orange
    "flame core") counts ZERO pixels in the host reference itself, because at the
    start map's light level the flame sprite renders pale warm grey with a peak
    around R=123, not saturated orange. That rule would have failed a known-good
    frame, which is how a gate becomes noise and gets switched off.

    Measured in these ROIs: host reference (torches present) 17 and 11 lit px;
    vkQuake grab (torches absent) 0 and 0, peaking at R=63..74. Roughly a 2x
    separation in peak brightness, so the threshold sits far from both.
    """
    scale = img.width / float(basis_w)
    x0, y0, x1, y1 = (int(round(v * scale)) for v in box)
    x0, y0 = max(0, x0), max(0, y0)
    x1, y1 = min(img.width, x1), min(img.height, y1)
    if x1 <= x0 or y1 <= y0:
        return 0
    crop = img.crop((x0, y0, x1, y1)).convert("RGB")
    # tobytes() rather than getdata(): getdata() is deprecated in Pillow 12 and
    # warns on every ROI of every frame, which is a lot of noise in a bench run.
    raw = crop.tobytes()
    n = 0
    for i in range(0, len(raw), 3):
        r, g, b = raw[i], raw[i + 1], raw[i + 2]
        if r > 100 and r > g + 20 and r > b + 20:
            n += 1
    return n


def _norm_thumb(im, size=(160, 90)):
    """160x90 grey thumb without the bottom HUD band (status bar, FPS counter), each pixel as
    standard deviations from the thumb's own mean: the brightness and contrast of the build
    (lightmap gamma, overbright) drop out, the scene's structure stays."""
    t = im.convert("L").resize(size, Image.BILINEAR).crop((0, 0, size[0], size[1] * 84 // 100))
    v = list(t.tobytes())
    mean = sum(v) / float(len(v))
    sd = (sum((x - mean) ** 2 for x in v) / float(len(v))) ** 0.5 or 1.0
    return [(x - mean) / sd for x in v]


def viewpoint_mae(path, ref_small, size=(160, 90)):
    """Mean absolute difference from the reference frame, in standard deviations, on
    brightness-normalised thumbs (_norm_thumb).

    The ROIs are fixed boxes, so they are only meaningful for a frame taken from
    the reference viewpoint (map start, spawn, angle 90). Feed this an arbitrary
    gameplay grab and it would report "torches absent" about a wall that is not
    even in shot -- the mirror image of the false positive that started all this.

    Until 2026-09-30 this compared raw grey levels, so a build that renders the same
    spot brighter (the P1 image's vkQuake: full status bar, FPS counter, lighter
    lightmaps) scored 12.8 > 8 on EVERY frame and the check turned INCONCLUSIVE
    although HDMI showed the spawn view with both torches lit. Normalised, the
    separation is wide: spawn-view grabs 0.07-0.13 (mig-vkq-h, p1-gate-vkq), demo
    attract mode / other maps 0.92-1.06 (p1-gate-qspasm, p1-gate-q2). Anything above
    the cutoff (default 0.4) is reported UNUSABLE, not FAIL.
    """
    a = _norm_thumb(Image.open(path), size)
    b = ref_small
    return sum(abs(x - y) for x, y in zip(a, b)) / float(len(a))


def classify(frames, args, spec, basis_w, required, thresh, ref_small):
    """Return (verdict, n_at_viewpoint, best_counts) for one trial's frames."""
    passed = 0
    at_vp = 0
    best = (0, 0)
    for path in frames:
        try:
            img = Image.open(path)
        except Exception:                                         # noqa: BLE001
            continue
        if ref_small is not None and viewpoint_mae(path, ref_small) > args.viewpoint_mae:
            continue
        at_vp += 1
        counts = [flame_pixels(img, r["box"], basis_w) for r in required]
        if min(counts) > min(best):
            best = tuple(counts[:2])
        if all(n >= thresh for n in counts):
            passed += 1
    if at_vp == 0:
        return ("INCONCLUSIVE", at_vp, best)
    if passed >= args.min_pass:
        return ("PRESENT", at_vp, best)
    return ("ABSENT", at_vp, best)


# A capture gap longer than this starts a new run. Frames inside one run land
# every ~15-25 s (capture-rpi4b-uart.sh's tick), and two runs are separated by a
# power cycle, so anything above a couple of minutes is a different run.
RUN_GAP_S = 120


def newest_run(paths):
    """Return (frames of the most recent run, count of older frames dropped)."""
    if not paths:
        return [], 0
    by_time = sorted(paths, key=lambda p: os.path.getmtime(p))
    group = [by_time[0]]
    for prev, cur in zip(by_time, by_time[1:]):
        if os.path.getmtime(cur) - os.path.getmtime(prev) > RUN_GAP_S:
            group = [cur]
        else:
            group.append(cur)
    return sorted(group), len(by_time) - len(group)


def rate_mode(args, spec, basis_w, required, ignored, thresh):
    """Score every trial of a test-cycle-bench run and report a pass RATE.

    A single boot cannot settle an intermittent render. #67 was "fixed" five
    times historically, each confirmed by one screenshot, and this project then
    measured the same configuration PRESENT twice and ABSENT six times -- so the
    unit of evidence has to be a rate over trials, not a verdict on one frame.

    INCONCLUSIVE is reported separately and never counted as a failure: it means
    no frame in that trial was taken at the reference viewpoint (the map did not
    load, or the console covered the view). The capture path has its own flake
    rate and lumping it into "absent" would understate the render.
    """
    ref_small = None
    if not args.no_viewpoint_check:
        ref_small = _norm_thumb(Image.open(args.reference))

    allf = hdmi_glob(os.path.join(REPO, "artifacts", "hdmi"), "*-%s-T*-*.png" % args.rate)
    if not allf:
        sys.exit("check-torch-rois: no frames for bench label %r "
                 "(expected artifacts/hdmi/*-%s-T<i>-*.png)" % (args.rate, args.rate))

    trials = {}
    for f in allf:
        m = re.search(r"-%s-(T\d+)-" % re.escape(args.rate), os.path.basename(f))
        if m:
            trials.setdefault(m.group(1), []).append(f)

    # Same hazard as --label: a re-used bench label mixes an earlier bench's
    # frames into the same T<i> bucket. Score only each trial's newest run.
    stale = 0
    for t in trials:
        trials[t], dropped = newest_run(trials[t])
        stale += dropped
    if stale:
        print("note: %d frame(s) from earlier runs of this bench label were NOT "
              "scored." % stale)

    print("bench label: %s   trials: %d   threshold: %d lit px   min-pass: %d frames"
          % (args.rate, len(trials), thresh, args.min_pass))
    print("")
    print("%-8s %-13s %-12s %s" % ("TRIAL", "VERDICT", "AT-VIEWPOINT", "BEST L/R"))

    tally = {"PRESENT": 0, "ABSENT": 0, "INCONCLUSIVE": 0}
    for t in sorted(trials, key=lambda x: int(x[1:])):
        verdict, at_vp, best = classify(trials[t], args, spec, basis_w, required,
                                        thresh, ref_small)
        tally[verdict] += 1
        print("%-8s %-13s %-12d %d / %d" % (t, verdict, at_vp, best[0],
                                            best[1] if len(best) > 1 else 0))

    gradeable = tally["PRESENT"] + tally["ABSENT"]
    print("")
    print("present %d   absent %d   inconclusive %d" %
          (tally["PRESENT"], tally["ABSENT"], tally["INCONCLUSIVE"]))
    if gradeable == 0:
        print("RATE: n/a -- every trial was inconclusive; the capture path, not the")
        print("render, is what needs fixing first.")
        return 2
    print("RATE: %d/%d gradeable trials rendered torches (%.0f%%)"
          % (tally["PRESENT"], gradeable, 100.0 * tally["PRESENT"] / gradeable))
    if tally["INCONCLUSIVE"]:
        print("NOTE: %d trial(s) never reached the reference viewpoint -- excluded from"
              % tally["INCONCLUSIVE"])
        print("the rate, not counted as failures. Capture flake, measure it separately.")
    return 0 if tally["PRESENT"] == gradeable else 1


def main():
    ap = argparse.ArgumentParser(add_help=True)
    ap.add_argument("frames", nargs="*", help="PNG frames to score")
    ap.add_argument("--spec", default=DEFAULT_SPEC)
    ap.add_argument("--label", help="score artifacts/hdmi/*-<label>-*.png instead")
    ap.add_argument("--min-pass", type=int, default=2,
                    help="frames that must pass for the cycle to pass (default 2)")
    ap.add_argument("--threshold", type=int, default=None,
                    help="override the per-ROI lit-pixel threshold")
    ap.add_argument("--reference", default=os.path.join(
        REPO, "docs", "misc", "torch-archaeology", "host-reference-start-map.png"),
        help="reference frame for the viewpoint check")
    ap.add_argument("--viewpoint-mae", type=float, default=0.4,
                    help="max brightness-normalised mean-abs-difference from the reference viewpoint, in standard deviations (default 0.4)")
    ap.add_argument("--no-viewpoint-check", action="store_true",
                    help="score the ROIs regardless of viewpoint (diagnostics only)")
    ap.add_argument("--rate", metavar="LABEL",
                    help="grade a test-cycle-bench run: score every <LABEL>-T<i> trial "
                         "and report a PASS RATE instead of a single verdict")
    args = ap.parse_args()

    with open(args.spec) as fh:
        spec = json.load(fh)
    basis_w = spec["_frame_basis"][0]
    required = [r for r in spec["rois"] if r.get("require") and not r.get("ignore")]
    ignored = [r for r in spec["rois"] if r.get("ignore")]
    thresh = args.threshold if args.threshold is not None else 8

    if args.rate:
        return rate_mode(args, spec, basis_w, required, ignored, thresh)

    frames = list(args.frames)
    if args.label:
        labelled = hdmi_glob(os.path.join(REPO, "artifacts", "hdmi"), "*-%s-*.png" % args.label)
        # ⚠ Until 2026-09-17 this took EVERY frame ever captured under the label,
        # with no lower time bound. artifacts/hdmi/ is cumulative and the gate
        # reuses one label per app, so frames from an earlier passing run could
        # satisfy --min-pass while THIS run's vkQuake never started. Keep only
        # the newest run, split on a capture gap the way check-hdmi-content.py
        # already does (frames inside a run are ~15-25 s apart).
        newest, dropped = newest_run(labelled)
        if dropped:
            print("note: %d older frame(s) under this label belong to earlier "
                  "runs and were NOT scored; scoring the newest %d."
                  % (dropped, len(newest)))
        frames += newest
    if not frames:
        sys.exit("check-torch-rois: no frames given (use --label or list PNGs)")

    print("spec: %s" % os.path.relpath(args.spec, REPO))
    print("required ROIs: %s   (threshold %d lit px)" %
          (", ".join(r["id"] for r in required), thresh))
    for r in ignored:
        print("IGNORED ROI: %s -- %s" % (r["id"], r.get("_why", "")[:80]))
    print("")

    ref_small = None
    if not args.no_viewpoint_check:
        try:
            ref_small = _norm_thumb(Image.open(args.reference))
        except Exception as exc:                                  # noqa: BLE001
            sys.exit("check-torch-rois: cannot open reference %s (%s)"
                     % (args.reference, exc))

    passed = 0
    unusable = 0
    for path in frames:
        try:
            img = Image.open(path)
        except Exception as exc:                                  # noqa: BLE001
            print("  %-52s UNREADABLE (%s)" % (os.path.basename(path), exc))
            continue

        mae = None
        if ref_small is not None:
            mae = viewpoint_mae(path, ref_small)
            if mae > args.viewpoint_mae:
                unusable += 1
                print("  %-52s UNUSABLE  viewpoint mae=%.1f > %.1f" %
                      (os.path.basename(path), mae, args.viewpoint_mae))
                continue

        counts = [(r["id"], flame_pixels(img, r["box"], basis_w)) for r in required]
        ok = all(n >= thresh for _, n in counts)
        passed += 1 if ok else 0
        print("  %-52s %s  mae=%4.1f  %s" % (
            os.path.basename(path),
            "TORCHES" if ok else "dark   ",
            mae if mae is not None else -1.0,
            "  ".join("%s=%d" % (i, n) for i, n in counts)))

    print("")
    print("frames: %d   at reference viewpoint: %d   passing: %d   required: %d"
          % (len(frames), len(frames) - unusable, passed, args.min_pass))
    if unusable == len(frames):
        print("RESULT: INCONCLUSIVE -- no frame was taken at the reference viewpoint.")
        print("Quake shows DEMO ATTRACT MODE when left at the menu, so idle grabs")
        print("wander the map and these fixed ROIs mean nothing. Capture with an")
        print("explicit `+map start` so the player sits at info_player_start.")
        return 2
    if passed >= args.min_pass:
        print("RESULT: torches present")
        return 0
    print("RESULT: TORCHES ABSENT -- do not report this render as fixed.")
    print("A moving misc_fireball lavaball sits just right of the archway and looks")
    print("like a flame; it is excluded on purpose. See")
    print("docs/misc/2026-09-03-quake-torch-regression-archaeology.md")
    return 1


if __name__ == "__main__":
    sys.exit(main())
