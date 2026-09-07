#!/usr/bin/env python3
"""Grade the LAST tick frame of each gated run: is there real content on screen?

The demo gate is log-based (glamor up, map loaded, shaders compiled). A screen
recording needs correct PIXELS, which logs cannot show. This is the cheap
pixel-side check over captures already on disk -- no Pi time.

Reported per run: fraction of non-black pixels, distinct-colour count, and mean
saturation. A rendered 3D scene or a populated desktop has many colours and
non-trivial saturation; a blank/failed frame is near-black or near-uniform.
NOTE: the FIRST frame of a cycle is a stale capture-card artefact, so only the
last tick before power-off is graded.
"""
import sys, os, glob, collections
from PIL import Image

runs = collections.defaultdict(list)
for p in glob.glob('artifacts/hdmi/*-tick.png'):
    base = os.path.basename(p)
    parts = base.split('-')
    if len(parts) < 4:
        continue
    label = '-'.join(parts[2:-1])
    runs[label].append(p)

want = [l for l in runs if any(l.startswith(x) for x in sys.argv[1:])] if len(sys.argv) > 1 else list(runs)
for label in sorted(want):
    frames = sorted(runs[label])
    if len(frames) < 2:
        continue
    p = frames[-1]
    im = Image.open(p).convert('RGB')
    im = im.resize((im.width // 4, im.height // 4))
    px = list(im.convert("RGB").tobytes())
    px = [tuple(px[i:i + 3]) for i in range(0, len(px), 3)]
    n = len(px)
    nonblack = sum(1 for r, g, b in px if (r + g + b) > 45)
    colours = len(set(px))
    sat = sum(max(r, g, b) - min(r, g, b) for r, g, b in px) / n
    verdict = 'CONTENT' if (nonblack / n > 0.25 and colours > 500) else 'SUSPECT'
    print(f"{label:<22} {verdict:<8} nonblack={nonblack/n*100:5.1f}%  colours={colours:<6} meansat={sat:5.1f}  ({len(frames)} ticks)")
