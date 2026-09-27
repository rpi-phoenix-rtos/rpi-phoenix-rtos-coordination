#!/usr/bin/env python3
"""Grade an HDMI calibration capture (docs/gpu-new-lane/hdmi-colour.md).

The Pi shows phoenix-calib-1920x1080.png full screen: 8 colour bars (rows 0-269),
a grey ramp v = x*256//1920 (rows 270-539), then R, G and B ramps. The capture card
delivers YUYV 4:2:2; this reads the RAW frame (no ffmpeg colour conversion in the way)
and reports:

  * Y of ramp input 0 and 255   -> full-range link (16/235) or not
  * distinct Y steps on the ramp -> end-to-end precision (220 = every limited-range Y code, i.e. 8 bpc intact)
  * Cb/Cr of the 100 % bars fitted against BT.601 and BT.709 -> the card's matrix

Usage: hdmi-calib-analyse.py <arm>-1080-yuyv.raw [more.raw ...]
"""
import sys

W, H = 1920, 1080


def load(path):
    data = open(path, "rb").read()
    if len(data) < W * H * 2:
        sys.exit(f"{path}: {len(data)} bytes, want {W * H * 2} (1920x1080 YUYV)")
    return data


def yuv(data, x, y):
    """Y, Cb, Cr of pixel (x, y); chroma is shared by the pixel pair."""
    o = (y * W + (x & ~1)) * 2
    Y = data[o + 2 * (x & 1)]
    return Y, data[o + 1], data[o + 3]


def mean_yuv(data, x0, x1, y0, y1):
    n = s0 = s1 = s2 = 0
    for y in range(y0, y1, 3):
        for x in range(x0, x1, 2):
            a, b, c = yuv(data, x, y)
            s0 += a; s1 += b; s2 += c; n += 1
    return s0 / n, s1 / n, s2 / n


def expected(rgb, kr, kb):
    """Limited-range YCbCr of a full-range RGB bar under the matrix (kr, kb)."""
    r, g, b = (v / 255 for v in rgb)
    kg = 1 - kr - kb
    ey = kr * r + kg * g + kb * b
    cb = (b - ey) / (2 * (1 - kb))
    cr = (r - ey) / (2 * (1 - kr))
    return 16 + 219 * ey, 128 + 224 * cb, 128 + 224 * cr


BARS = [(255, 255, 255), (255, 255, 0), (0, 255, 255), (0, 255, 0),
        (255, 0, 255), (255, 0, 0), (0, 0, 255), (0, 0, 0)]
MATRICES = {"BT.601": (0.299, 0.114), "BT.709": (0.2126, 0.0722)}


def analyse(path):
    d = load(path)
    print(f"== {path}")
    # grey ramp: middle rows of the band, one sample per ramp step
    steps = []
    for v in range(256):
        x0 = (v * W + 255) // 256
        x1 = ((v + 1) * W + 255) // 256
        xs = range(x0 + 1, max(x0 + 2, x1 - 1))
        ys = range(330, 480, 5)
        tot = [yuv(d, x, y)[0] for x in xs for y in ys]
        steps.append(sum(tot) / len(tot))
    distinct = len({round(s) for s in steps})
    mono = sum(1 for a, b in zip(steps, steps[1:]) if b < a - 0.5)
    lo, hi = steps[0], steps[255]
    slope = (hi - lo) / 255
    print(f"ramp  Y(0)={lo:.1f} Y(255)={hi:.1f} slope={slope:.3f}/step  distinct={distinct} (limited-range Y has 220 codes; ~64 would mean 6 bpc)  "
          f"reversals={mono}")
    if abs(lo - 16) < 3 and abs(hi - 235) < 3:
        verdict = "full-range link read correctly (0->16, 255->235)"
    elif abs(lo - 16 - 219 * 16 / 255) < 4 and abs(hi - 16 - 219 * 235 / 255) < 4:
        verdict = "LIMITED-range link read as full (0->~30, 255->~218: contrast lost twice)"
    elif lo < 8 and hi > 245:
        verdict = "limited-range link EXPANDED past Y 16..235 (clipping at both ends)"
    else:
        verdict = "unclassified"
    print(f"range {verdict}")
    # colour bars
    meas = []
    for i, c in enumerate(BARS):
        x0 = i * W // 8 + 20
        meas.append((c, mean_yuv(d, x0, x0 + W // 8 - 40, 40, 230)))
    for name, (kr, kb) in MATRICES.items():
        err = 0.0
        for c, m in meas:
            e = expected(c, kr, kb)
            err += (m[1] - e[1]) ** 2 + (m[2] - e[2]) ** 2  # chroma only: Y also carries the range
        print(f"bars  {name}: chroma rms error {(err / (2 * len(meas))) ** 0.5:.1f}")
    for c, m in meas:
        print(f"      bar {c!s:15} Y={m[0]:6.1f} Cb={m[1]:6.1f} Cr={m[2]:6.1f}")


for p in sys.argv[1:]:
    analyse(p)
