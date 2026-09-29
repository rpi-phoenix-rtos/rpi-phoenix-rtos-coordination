#!/usr/bin/env python3
"""Grade the LAST tick frame of each gated run: is there real content on screen?

The demo gate is log-based (glamor up, map loaded, shaders compiled). A screen
recording needs correct PIXELS, which logs cannot show. This is the cheap
pixel-side check over captures already on disk -- no Pi time.

Reported per run: fraction of non-black pixels, distinct-colour count, and mean
saturation. A rendered 3D scene or a populated desktop has many colours and
non-trivial saturation; a blank/failed frame is near-black or near-uniform.
NOTE: the FIRST frame of a cycle is a stale capture-card artefact and is skipped.
The verdict comes from the BEST remaining tick, with the last one reported
alongside: apps like QuakeSpasm have no controlled viewpoint (they fall through
to the demo attract loop), so grading the last frame alone is a lottery that
reads as a regression when the scene simply happened to be dark.
"""
import sys, os, glob, collections

from PIL import Image


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


# Group by LABEL *and* by RUN. Labels repeat every time a gate is re-run, and
# keying on the label alone silently pools frames from different runs into one
# "best" -- which made a post-change gate report numbers identical to the digit
# with the pre-change one, because the winning frame came from the older run.
# Frames inside a run are ~15 s apart, so a gap of more than RUN_GAP_S starts a
# new run.
RUN_GAP_S = 120

def _stamp(base):
    d, t = base.split('-')[0], base.split('-')[1]
    return (int(d[0:4]), int(d[4:6]), int(d[6:8]), int(t[0:2]), int(t[2:4]), int(t[4:6]))

def _secs(s):
    import calendar, datetime
    return calendar.timegm(datetime.datetime(*s).timetuple())

bylabel = collections.defaultdict(list)
for p in hdmi_glob('artifacts/hdmi', '*-tick.png'):
    base = os.path.basename(p)
    parts = base.split('-')
    if len(parts) < 4:
        continue
    try:
        ts = _secs(_stamp(base))
    except Exception:
        continue
    bylabel['-'.join(parts[2:-1])].append((ts, p))

runs = collections.defaultdict(list)
for label, items in bylabel.items():
    items.sort()
    run_no, prev = 0, None
    for ts, p in items:
        if prev is not None and (ts - prev) > RUN_GAP_S:
            run_no += 1
        prev = ts
        runs[label if run_no == 0 else '%s#%d' % (label, run_no)].append(p)

want = [l for l in runs if any(l.startswith(x) for x in sys.argv[1:])] if len(sys.argv) > 1 else list(runs)
for label in sorted(want):
    frames = sorted(runs[label])
    if len(frames) < 2:
        continue

    def grade(path):
        im = Image.open(path).convert('RGB')
        im = im.resize((im.width // 4, im.height // 4))
        px = list(im.convert("RGB").tobytes())
        px = [tuple(px[i:i + 3]) for i in range(0, len(px), 3)]
        n = len(px)
        nonblack = sum(1 for r, g, b in px if (r + g + b) > 45)
        colours = len(set(px))
        sat = sum(max(r, g, b) - min(r, g, b) for r, g, b in px) / n
        return nonblack / n, colours, sat

    # Grade the BEST tick, not just the last. Several of these apps have NO controlled
    # viewpoint -- QuakeSpasm falls through to the demo attract loop, so which scene the
    # last frame caught is a lottery. Measured 2026-09-12: one qspasm run graded 11.4%
    # nonblack on its last frame while an earlier tick of the SAME run was 100%, and it had
    # rendered 9569 frames at 31 fps with 0 faults. Judging that run on its last frame alone
    # reads as a regression and is not one. Skip frame 0 (stale capture-card artefact).
    # "Did any tick draw a real scene?" -- so pick the best PASSING frame, and only fall back
    # to the max-nonblack one if none passes. Maximising nonblack first is wrong: a solid
    # bright splash scores 100% nonblack with ~300 colours and then fails the colour test,
    # which is how an earlier version of this change invented a fresh false SUSPECT for the
    # X desktop while its last frame was a perfectly good 89.7% / 9535 colours.
    scored = [(grade(f), f) for f in frames[1:]]
    passing = [t for t in scored if t[0][0] > 0.25 and t[0][1] > 500]
    if passing:
        (bn, bc, bs), bf = max(passing, key=lambda t: t[0][1])
        verdict = 'CONTENT'
    else:
        (bn, bc, bs), bf = max(scored, key=lambda t: (t[0][0], t[0][1]))
        verdict = 'SUSPECT'
    (ln, lc, ls), _ = scored[-1]
    print(f"{label:<22} {verdict:<8} best: nonblack={bn*100:5.1f}%  colours={bc:<6} meansat={bs:5.1f}"
          f"   last: nonblack={ln*100:5.1f}%  colours={lc:<6}  ({len(frames)} ticks)")
