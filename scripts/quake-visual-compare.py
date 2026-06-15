#!/usr/bin/env python3
"""
quake-visual-compare.py — automated Pi(V3D)-vs-host(llvmpipe) visual diff for the
deterministic Quake demo capture. Pairs cap_NNNN.tga by index (same demo moment,
thanks to host_framerate + r_particles 0) and reports per-frame metrics + the
specific bug signatures the user flagged: objects rendering BLACK instead of
textured, and broken TEXT.

Run with the harness venv:
  .venv-quakecmp/bin/python scripts/quake-visual-compare.py --pi <dir> --host <dir> [--out <dir>]

Metrics per frame:
  ssim        structural similarity (grayscale, 0..1; 1=identical)
  mae         mean abs RGB error (0..255)
  blacktex%   % of pixels TEXTURED on host but BLACK on Pi  <-- the "black object" bug
  hud_ssim    ssim of the bottom HUD/status strip           <-- proxy for text/sbar bugs
Outputs a CSV, a console summary (worst frames), and side-by-side+diff montages
for the worst offenders (so a human can eyeball exactly what's missing).
"""
import argparse, glob, os, sys
import numpy as np
from PIL import Image
from skimage.metrics import structural_similarity as ssim


def load(path):
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.uint8)


def lum(rgb):
    return rgb[..., 0] * 0.299 + rgb[..., 1] * 0.587 + rgb[..., 2] * 0.114


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pi", required=True, help="dir of Pi cap_*.tga")
    ap.add_argument("--host", required=True, help="dir of host cap_*.tga")
    ap.add_argument("--out", default="artifacts/quake-compare", help="report/montage dir")
    ap.add_argument("--montage-worst", type=int, default=6)
    ap.add_argument("--black", type=float, default=16.0, help="Pi luminance <= this = black")
    ap.add_argument("--textured", type=float, default=32.0, help="host luminance >= this = textured")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)

    def idx_of(p):
        return os.path.basename(p).split("cap_")[1].split(".")[0]
    pi = {idx_of(p): p for p in glob.glob(os.path.join(args.pi, "cap_*.tga"))}
    host = {idx_of(p): p for p in glob.glob(os.path.join(args.host, "cap_*.tga"))}
    common = sorted(set(pi) & set(host))
    if not common:
        print(f"no common frames (pi={len(pi)} host={len(host)})", file=sys.stderr)
        return 2
    print(f"comparing {len(common)} frame pairs")

    rows = []
    for k in common:
        a, b = load(pi[k]), load(host[k])
        if a.shape != b.shape:
            h = min(a.shape[0], b.shape[0]); w = min(a.shape[1], b.shape[1])
            a, b = a[:h, :w], b[:h, :w]
        la, lb = lum(a), lum(b)
        s = ssim(la, lb, data_range=255)
        mae = float(np.abs(a.astype(int) - b.astype(int)).mean())
        blacktex = float(((lb >= args.textured) & (la <= args.black)).mean() * 100.0)
        hud_h = max(1, int(a.shape[0] * 0.16))  # bottom strip = sbar/text
        hud = ssim(la[-hud_h:], lb[-hud_h:], data_range=255)
        rows.append((k, s, mae, blacktex, hud))

    # CSV
    csv = os.path.join(args.out, "compare.csv")
    with open(csv, "w") as f:
        f.write("frame,ssim,mae,blacktex_pct,hud_ssim\n")
        for k, s, mae, bt, hud in rows:
            f.write(f"{k},{s:.4f},{mae:.2f},{bt:.3f},{hud:.4f}\n")

    ss = np.array([r[1] for r in rows]); bt = np.array([r[3] for r in rows])
    print(f"\nSSIM      mean={ss.mean():.3f} min={ss.min():.3f}")
    print(f"blacktex% mean={bt.mean():.3f} max={bt.max():.3f}")
    print(f"\nworst frames by blacktex% (the 'black object' bug):")
    for k, s, mae, b, hud in sorted(rows, key=lambda r: -r[3])[:8]:
        print(f"  cap_{k}: blacktex={b:.2f}%  ssim={s:.3f}  hud_ssim={hud:.3f}  mae={mae:.1f}")

    # montages of worst-by-blacktex: [pi | host | diff-heatmap]
    worst = sorted(rows, key=lambda r: -r[3])[:args.montage_worst]
    for k, *_ in worst:
        a, b = load(pi[k]), load(host[k])
        h = min(a.shape[0], b.shape[0]); w = min(a.shape[1], b.shape[1])
        a, b = a[:h, :w], b[:h, :w]
        diff = np.abs(a.astype(int) - b.astype(int)).sum(axis=2)
        dh = np.zeros_like(a)
        dh[..., 0] = np.clip(diff, 0, 255)  # red = magnitude of difference
        mont = np.concatenate([a, b, dh], axis=1)
        Image.fromarray(mont.astype(np.uint8)).save(os.path.join(args.out, f"montage_cap_{k}.png"))
    print(f"\nCSV: {csv}\nmontages ([Pi | host | diff]): {args.out}/montage_cap_*.png")
    return 0


if __name__ == "__main__":
    sys.exit(main())
