#!/usr/bin/env python3
"""
flicker-analyze.py — detect one-frame "blink" flicker in frames grabbed from the
Pi's HDMI output (see flicker-capture-analyze.sh).

Two problems the earlier version got wrong (both fixed here):

  1. FATAL COUNTER BUG. The white-pixel counter was `sum(histogram()[256:])`. A
     PIL histogram has exactly 256 bins (0..255); white after thresholding sits at
     index 255, so `[256:]` is an EMPTY slice and the count was ALWAYS 0. Every
     prior run therefore reported "no flicker" no matter what was on screen. The
     counter is now `histogram()[255]`.

  2. STATIC-CAMERA ASSUMPTION + 60/30 DUPLICATION. The old blink test required the
     time-neighbours to AGREE (|N-1 - N+1| < STABLE). Quake demos pan constantly,
     so neighbours never agree and nothing is ever flagged. And the USB grab is
     ~60fps of a ~30fps source, so every quake frame is duplicated, which breaks
     any triplet logic. We now (a) de-duplicate consecutive near-identical grab
     frames down to the unique quake-frame sequence, then (b) flag a pixel that
     differs from BOTH time-neighbours: (|N-(N-1)|>TH) AND (|N-(N+1)|>TH). That is
     motion-robust (smooth motion is monotonic, so at most one side differs) and
     cut-robust (at a demo cut N~=N-1 on one side), while a genuine one-frame
     blink/pop differs from both.

CAVEAT: legitimate one-frame VFX (explosion onset, muzzle flash) also differ from
both neighbours and will score high. Absolute detection cannot separate "flicker"
from "explosion"; use this to A/B two builds on the SAME demo (a regression shows
as MORE blink pixels on entity/world regions) and eyeball the saved montages.

  flicker-analyze.py <frames_dir> <out_dir> [--th 45] [--dup 1.2] [--scale 2]

PIL only (no numpy).
"""
import sys, os, glob
from PIL import Image, ImageChops


def main():
    if len(sys.argv) < 3:
        print("usage: flicker-analyze.py <frames_dir> <out_dir> [--th N] [--dup F] [--scale N]")
        sys.exit(2)
    frames_dir, out_dir = sys.argv[1], sys.argv[2]
    TH, DUP, SCALE = 45, 1.2, 2
    a = sys.argv[3:]
    for i, tok in enumerate(a):
        if tok == "--th": TH = int(a[i + 1])
        elif tok == "--dup": DUP = float(a[i + 1])
        elif tok == "--scale": SCALE = int(a[i + 1])

    files = sorted(glob.glob(os.path.join(frames_dir, "*.png")))
    if len(files) < 3:
        print("too few frames:", len(files)); sys.exit(1)

    def meandiff(x, y):
        h = ImageChops.difference(x, y).histogram(); t = sum(h)
        return (sum(i * c for i, c in enumerate(h)) / t) if t else 0.0

    gray = []
    for f in files:
        im = Image.open(f).convert("L")
        if SCALE > 1:
            im = im.resize((im.width // SCALE, im.height // SCALE))
        gray.append(im)

    # De-duplicate the 60->30fps grab: keep a frame only if it differs enough from
    # the last kept one. Leaves the true unique quake-frame sequence.
    keep = [0]
    for i in range(1, len(gray)):
        if meandiff(gray[i], gray[keep[-1]]) >= DUP:
            keep.append(i)

    w, h = gray[0].size
    tot = w * h
    print(f"grab frames: {len(files)}  unique (dedup dup={DUP}): {len(keep)}  "
          f"analysis-res: {w}x{h}  TH={TH}")

    def thr(d): return d.point(lambda x: 255 if x > TH else 0)   # mode "L", 0/255
    def wc(im): return im.histogram()[255]                        # white-pixel count (FIXED)

    scores = []
    for k in range(1, len(keep) - 1):
        pa, pb, pc = gray[keep[k - 1]], gray[keep[k]], gray[keep[k + 1]]
        dprev = thr(ImageChops.difference(pb, pa))
        dnext = thr(ImageChops.difference(pb, pc))
        blink = ImageChops.multiply(dprev, dnext)    # per-pixel AND for 0/255 images
        scores.append((keep[k], keep[k - 1], keep[k + 1], wc(blink)))

    vals = [s for _, _, _, s in scores]
    mean = sum(vals) / len(vals) if vals else 0
    scores.sort(key=lambda x: -x[3])
    print(f"mean blink px/uframe: {mean:.1f} ({100 * mean / tot:.3f}%)   "
          f"max: {scores[0][3]} @ {os.path.basename(files[scores[0][0]])}")
    print("worst frames (frame : blink px : pct):")
    for n, _, _, s in scores[:10]:
        print(f"  {os.path.basename(files[n]):12s} : {s:6d} : {100.0 * s / tot:.2f}%")

    thresh = max(50, tot // 400)  # 0.25% of frame
    flick = [n for n, _, _, s in scores if s > thresh]
    print(f"frames with >{thresh} blink px (>0.25%): {len(flick)} / {len(scores)}")
    print("VERDICT:", "BLINK EVENTS PRESENT (inspect montages — VFX or flicker?)"
          if len(flick) >= 3 else "no significant one-frame blink")

    # Save the worst triplets full-res + blink mask, for eyeball confirmation.
    for rank, (n, ai, ci, s) in enumerate(scores[:4]):
        if s < thresh:
            break
        trip = [Image.open(files[ai]).convert("RGB"),
                Image.open(files[n]).convert("RGB"),
                Image.open(files[ci]).convert("RGB")]
        ga = [t.convert("L") for t in trip]
        mask = ImageChops.multiply(thr(ImageChops.difference(ga[1], ga[0])),
                                   thr(ImageChops.difference(ga[1], ga[2]))).convert("RGB")
        row = trip + [mask]
        tw = sum(im.width for im in row); th = max(im.height for im in row)
        canvas = Image.new("RGB", (tw, th))
        x = 0
        for im in row:
            canvas.paste(im, (x, 0)); x += im.width
        canvas = canvas.resize((tw // 3, th // 3))
        outp = os.path.join(out_dir, f"blink_{rank}_{os.path.basename(files[n])}")
        canvas.save(outp)
        print("  saved montage:", outp)


if __name__ == "__main__":
    main()
