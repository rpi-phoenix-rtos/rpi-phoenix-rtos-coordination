#!/usr/bin/env python3
"""Grade an assembled demo reel segment by segment, before anyone publishes it.

Three things have gone wrong with cuts in this project, and this checks all three:

1. A cut that runs past the end of its source run, into the capture card's
   NO-SIGNAL field -- a uniform near-black frame the card emits once the Pi is
   powered off. That is not a black *picture*, it is a dead input, and it looks
   identical to a broken segment in the published file.
2. A dead or near-dead segment (nothing on screen).
3. A frozen segment. ⚠ Judged only for segments declared MOTION; a console or a
   rendered web page is legitimately static, and grading those on motion is how a
   perfectly good text segment gets called broken.

Usage:  scripts/verify-demo-reel.py <reel.mp4> [--segments scripts/make-demo-reel.sh]

Copyright 2026 Phoenix Systems
SPDX-License-Identifier: BSD-3-Clause
"""
import argparse, os, subprocess, sys
import numpy as np

# Segments whose content is legitimately still; everything else must move.
STATIC_OK = ("Shell", "Dillo", "Boot", "Atril")  # a document on screen, like a web page

def frames(path, w=480, h=270, fps=2):
    b = subprocess.run(["ffmpeg", "-loglevel", "error", "-i", path,
                        "-vf", f"fps={fps},scale={w}:{h}", "-f", "rawvideo",
                        "-pix_fmt", "rgb24", "-"], capture_output=True).stdout
    n = len(b) // (w * h * 3)
    if n == 0:
        sys.exit(f"verify-demo-reel: no frames decoded from {path}")
    return np.frombuffer(b, np.uint8)[:n*w*h*3].reshape(n, h, w, 3).astype(np.float32), fps

def segments(script):
    """(caption prefix, length) per segment line "<clip>|<start>|<length s>|<label>".

    The lines of a reel script's segments=() table (quoted), or of a plain segments file
    (make-browser-reel.sh's BROWSER_REEL_SEGMENTS, quotes optional). A line counts when its third
    field is a whole number of seconds; anything else (comments, code) is not a segment.
    """
    out = []
    for line in open(script):
        s = line.strip()
        if s.startswith("#") or s.count("|") < 3:
            continue
        clip, start, ln, label = s.strip('"').split("|", 3)
        if not ln.isdigit() or not clip or " " in clip:
            continue
        out.append((label.split("—")[0].strip(), int(ln)))
    return out

# Where make-demo-reel.sh draws the caption: a 64 px band whose BOTTOM edge sits
# 136 px above the frame's bottom (drawbox y=ih-200, h=64). It is deliberately not
# flush with the bottom, because that is where video players draw their controls.
# Keep these two in step with the script or this check silently measures nothing.
LABEL_BAND = (200, 136)   # (px above bottom: top edge, bottom edge)


def save_caption_frames(reel, segs, outdir="artifacts/reel-captions"):
    """Save one frame per segment, 1 s in, while the caption is up — for a HUMAN.

    ⚠ This deliberately renders NO verdict. Four automated versions were tried and
    every one was confidently wrong:
      1. "bright pixels in the bottom 64 px" counted Quake II's ammo strip and
         SuperTuxKart's speedometer as a caption;
      2. the same test broke completely when the caption moved off the bottom edge;
      3. "the backing box is darker than the picture below" failed on every dark
         scene, because black@0.62 over a dark frame is not darker;
      4. "the band changed between 1 s and 8 s" scored 9 of 11 segments of a
         bottom-captioned reel as captioned, because content motion changes the
         band too.
    Caption presence, position and legibility are a visual judgement. The tool's
    job is to put the frames in front of someone, not to pretend it can read them.
    """
    os.makedirs(outdir, exist_ok=True)
    t, saved = 0, []
    for i, (name, L) in enumerate(segs, 1):
        f = os.path.join(outdir, "seg%02d.png" % i)
        subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", reel, "-ss", str(t + 1),
                        "-frames:v", "1", "-vf", "scale=640:-1", f], capture_output=True)
        t += L
        if os.path.exists(f):
            saved.append(f)
    return saved


def segment_signatures(vid, segs, fps):
    """One normalised appearance signature per segment (mean frame, mean-removed).

    Catches a segment that LOOKS like another one. That is not a cosmetic concern:
    the 2026-09-16 "non-personal" variant sourced its H.265 segment from a video of
    the showcase itself, so the hardware-decode segment showed Quake II gameplay --
    complete with a burned-in fps counter contradicting its own caption -- and was
    indistinguishable from a game segment once the caption faded. Every other check
    here passed it. The owner spotted it by watching.
    """
    sigs, t = [], 0
    for name, L in segs:
        i0, i1 = int((t + 2) * fps), int((t + L - 2) * fps)
        seg = vid[i0:i1]
        t += L
        if len(seg) == 0:
            sigs.append((name, None)); continue
        m = seg.mean(axis=0).mean(axis=2)          # mean frame, grayscale
        m = m - m.mean()
        n = float(np.sqrt((m * m).sum())) or 1.0
        sigs.append((name, m / n))
    return sigs


def lookalike_report(vid, segs, fps, thresh=0.80):
    """Pairs of DIFFERENT segments whose appearance correlates above `thresh`."""
    sigs = segment_signatures(vid, segs, fps)
    out = []
    for i in range(len(sigs)):
        for j in range(i + 1, len(sigs)):
            (na, a), (nb, b) = sigs[i], sigs[j]
            if a is None or b is None:
                continue
            c = float((a * b).sum())
            if c >= thresh:
                out.append((na, nb, c))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("reel")
    ap.add_argument("--segments", default="scripts/make-demo-reel.sh")
    ap.add_argument("--static-ok", default="",
                    help="more caption prefixes (comma-separated) whose segments may be still, "
                         "e.g. 'WPE WebKit,WebKitGTK' for web pages between keystrokes")
    a = ap.parse_args()
    static_ok = STATIC_OK + tuple(p.strip() for p in a.static_ok.split(",") if p.strip())

    segs = segments(a.segments)
    # ⚠ Zero segments graded as PASS until 2026-09-17: an empty list runs no
    # loop, leaves bad == 0 and prints "every segment has a live signal". The
    # parser keys off make-demo-reel.sh's line format, so a format change made
    # this checker certify a reel it had not looked at.
    if not segs:
        print(f"FAIL — parsed NO segments out of {a.segments}; this check "
              f"measured nothing. Has the segment-table line format changed?")
        return 1
    vid, fps = frames(a.reel)
    g = vid.mean(axis=3)
    mot = np.zeros(len(g)); mot[1:] = np.abs(np.diff(g, axis=0)).mean(axis=(1, 2))


    print(f"{'segment':14s} {'window':>12s} {'lum':>6s} {'colours':>8s} {'motion':>7s} {'still%':>7s}  verdict")
    t, bad = 0, 0
    for name, L in segs:
        i0, i1 = int(t*fps) + 2, int((t+L)*fps) - 2      # trim the label/crossfade edges
        seg, mo = vid[i0:i1], mot[i0+1:i1]
        # ⚠ An empty window used to grade "ok": seg.mean() is nan, every
        # threshold test against nan is False, and the segment passed without a
        # single frame behind it. It happens whenever the reel is SHORTER than
        # the declared segment table — exactly the case this check exists for.
        if len(seg) < 4 or len(mo) < 2:
            print(f"{name:14s} {t:4.0f}-{t+L:4.0f}s {'':6s} {'':8s} "
                  f"{'':7s} {'':6s}   NO FRAMES ({len(seg)} in window; reel is "
                  f"{len(vid)/fps:.0f}s, table declares {sum(x[1] for x in segs)}s)")
            bad += 1
            t += L
            continue
        lum = seg.mean()
        cols = len(np.unique((seg // 16).astype(np.uint8).reshape(-1, 3), axis=0))
        still = 100.0 * (mo < 0.3).mean()
        # no-signal: a uniform field, i.e. no spatial structure at all
        flat = float(np.mean([f.std() for f in seg[-4:]]))
        v = []
        if flat < 1.5 and lum < 12: v.append("DEAD SIGNAL")
        elif lum < 6:               v.append("DARK")
        if still > 60 and not name.startswith(static_ok): v.append("FROZEN")
        bad += len(v)
        print(f"{name:14s} {t:4.0f}-{t+L:4.0f}s {lum:6.1f} {cols:8d} "
              f"{mo.mean():7.2f} {still:6.0f}%  {'; '.join(v) if v else 'ok'}")
        t += L
    for na, nb, c in lookalike_report(vid, segs, fps):
        print(f"LOOKALIKE: '{na}' and '{nb}' correlate {c:.2f} — do they show the "
              f"same thing? a segment must read as what its caption claims")
        bad += 1

    shots = save_caption_frames(a.reel, segs)
    if shots:
        print(f"\ncaption frames for visual review: {os.path.dirname(shots[0])}/  "
              f"({len(shots)} segments) — check each caption is present, legible, "
              f"clear of the player's controls, and matches what is on screen")

    print("PASS — every segment has a live signal and content" if bad == 0
          else f"FAIL — {bad} issue(s)")
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main())
