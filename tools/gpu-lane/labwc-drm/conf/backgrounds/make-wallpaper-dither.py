#!/usr/bin/env python3
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: CC0-1.0 (the generated image is public domain)
"""labwc-drm: generate phoenix-gradient-dither-1920x1080.png, the dithered M7 wallpaper.

make-wallpaper.py's gradient quantised straight to 8 bits per channel has flat runs of
~11 px (up to 29) per colour along a row, which band on any 8 bpc display
(docs/gpu-new-lane/hdmi-colour.md). This takes that 8-bit image, smooths each row in
float (a 49-px horizontal box filter: the steps become the continuous gradient again)
and quantises it back to 8 bits with a little noise (+-0.25) and error feedback (half
the rounding error carried to the next pixel): runs of ~1.4 px (max 14), no visible
bands. Deterministic (random.seed(1)); stdlib only; a few seconds.
Usage: make-wallpaper-dither.py <out.png>
"""
import importlib.util
import os
import random
import struct
import sys
import zlib

W, H = 1920, 1080
RADIUS = 24            # box filter: 2 * RADIUS + 1 pixels
NOISE = 0.5            # uniform noise, peak to peak, in 8-bit steps
FEEDBACK = 0.5         # share of the rounding error carried to the next pixel


def base_pixel():
    """make-wallpaper.py's pixel(x, y): the 8-bit source of this image."""
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'make-wallpaper.py')
    spec = importlib.util.spec_from_file_location('make_wallpaper', path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod.pixel


def main():
    pixel = base_pixel()
    random.seed(1)
    rows = bytearray()
    for y in range(H):
        row = [pixel(x, y) for x in range(W)]
        # prefix sums per channel for the box filter
        acc = [[0, 0, 0]]
        for p in row:
            last = acc[-1]
            acc.append([last[0] + p[0], last[1] + p[1], last[2] + p[2]])
        err = [0.0, 0.0, 0.0]
        rows.append(0)                                      # filter: none
        for x in range(W):
            a, b = max(0, x - RADIUS), min(W, x + RADIUS + 1)
            n = b - a
            for c in range(3):
                v = (acc[b][c] - acc[a][c]) / n + err[c] + (random.random() - 0.5) * NOISE
                q = max(0, min(255, int(round(v))))
                err[c] = (v - q) * FEEDBACK
                rows.append(q)

    def chunk(tag, data):
        c = struct.pack('>I', len(data)) + tag + data
        return c + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff)

    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(bytes(rows), 9)) + chunk(b'IEND', b'')
    with open(sys.argv[1], 'wb') as f:
        f.write(png)


if __name__ == '__main__':
    main()
