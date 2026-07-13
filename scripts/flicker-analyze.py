#!/usr/bin/env python3
"""
flicker-analyze.py — detect single-frame "blink" flicker in a sequence of frames
grabbed from the Pi's HDMI output (see flicker-capture-analyze.sh).

Flicker signature: a region that is present in frame N-1 and N+1 but changes
sharply in frame N (entity blinks out/in), while N-1 and N+1 AGREE. Smooth camera
motion changes N-1,N,N+1 progressively (neighbours disagree) and is NOT flagged.

  flicker-analyze.py <frames_dir> <out_dir> [--th 38] [--stable 16] [--scale 3]

Prints a per-frame blink score and the worst frames; saves the worst blink
triplets (N-1,N,N+1) side-by-side to <out_dir>/blink_*.png for visual confirmation.
PIL only (no numpy).
"""
import sys, os, glob
from PIL import Image, ImageChops

def main():
    if len(sys.argv) < 3:
        print("usage: flicker-analyze.py <frames_dir> <out_dir> [--th N] [--stable N] [--scale N]")
        sys.exit(2)
    frames_dir, out_dir = sys.argv[1], sys.argv[2]
    TH, STABLE, SCALE = 38, 16, 3
    a = sys.argv[3:]
    for i, tok in enumerate(a):
        if tok == "--th": TH = int(a[i+1])
        elif tok == "--stable": STABLE = int(a[i+1])
        elif tok == "--scale": SCALE = int(a[i+1])

    files = sorted(glob.glob(os.path.join(frames_dir, "*.png")))
    if len(files) < 3:
        print("too few frames:", len(files)); sys.exit(1)

    # Load downscaled grayscale (speed + denoise). Keep full-res paths for montage.
    gray = []
    for f in files:
        im = Image.open(f).convert("L")
        if SCALE > 1:
            im = im.resize((im.width // SCALE, im.height // SCALE))
        gray.append(im)
    w, h = gray[0].size
    tot = w * h
    print(f"frames: {len(files)}  analysis-res: {w}x{h}  TH={TH} STABLE={STABLE}")

    def hi(d, t): return d.point(lambda x: 255 if x > t else 0).convert("1")
    def lo(d, t): return d.point(lambda x: 255 if x < t else 0).convert("1")
    def whitecount(im): return sum(im.histogram()[256:])

    scores = []
    for n in range(1, len(gray) - 1):
        pa, pb, pc = gray[n-1], gray[n], gray[n+1]
        dprev = hi(ImageChops.difference(pb, pa), TH)
        dnext = hi(ImageChops.difference(pb, pc), TH)
        dstab = lo(ImageChops.difference(pa, pc), STABLE)
        blink = ImageChops.logical_and(ImageChops.logical_and(dprev, dnext), dstab)
        scores.append((n, whitecount(blink)))

    vals = [s for _, s in scores]
    mean = sum(vals) / len(vals) if vals else 0
    scores_sorted = sorted(scores, key=lambda x: -x[1])
    print(f"mean blink px/frame: {mean:.1f} ({100*mean/tot:.3f}%)   max: {scores_sorted[0][1]} @ frame {scores_sorted[0][0]}")
    print("worst frames (idx : blink px : pct):")
    for n, s in scores_sorted[:10]:
        print(f"  {n:04d} : {s:6d} : {100.0*s/tot:.2f}%")

    # A flicker run typically shows many frames with elevated blink counts, not a
    # lone spike. Report how many frames exceed a modest fraction of the frame.
    thresh = max(50, tot // 400)  # 0.25% of frame
    flick_frames = [n for n, s in scores if s > thresh]
    print(f"frames with >{thresh} blink px (>0.25%): {len(flick_frames)} / {len(scores)}")
    verdict = "FLICKER DETECTED" if len(flick_frames) >= 3 else "no significant flicker"
    print("VERDICT:", verdict)

    # Save the worst triplets (full-res) side by side for visual confirmation.
    for rank, (n, s) in enumerate(scores_sorted[:3]):
        if s < thresh: break
        trip = [Image.open(files[n-1]), Image.open(files[n]), Image.open(files[n+1])]
        tw = sum(im.width for im in trip); th = max(im.height for im in trip)
        canvas = Image.new("RGB", (tw, th))
        x = 0
        for im in trip:
            canvas.paste(im.convert("RGB"), (x, 0)); x += im.width
        canvas = canvas.resize((tw // 2, th // 2))
        outp = os.path.join(out_dir, f"blink_{rank}_frames_{n-1}-{n}-{n+1}.png")
        canvas.save(outp)
        print("  saved triplet:", outp)

if __name__ == "__main__":
    main()
