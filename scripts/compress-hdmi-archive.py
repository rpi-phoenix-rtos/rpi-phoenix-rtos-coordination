#!/usr/bin/env python3
"""Shrink an archived month of HDMI snapshots in place: every PNG becomes a WebP of the same name.

Each file is encoded twice and the smaller useful result is kept:

  lossless WebP      pixel-exact; wins on console/desktop frames (text, flat UI) -- used unless
  lossy WebP q=92    it is under 70 % of the lossless size (games, video, photos), where the loss
                     is invisible (mean absolute error ~1 level of 255 per channel)

Every WebP is decoded and compared (size, mode; lossless ones pixel-for-pixel) before its PNG is
removed; the file's modification time is kept. A record of each choice is appended to
<month>/COMPRESSION.tsv (name, mode, png bytes, webp bytes). Git-tracked files are kept as PNG; the
current month's folder only with --include-current-month. Viewable with any browser or image viewer (WebP).

Usage: scripts/compress-hdmi-archive.py artifacts/hdmi/2026-08 [--jobs N] [--dry-run] [--include-current-month]
       (in the background: nice -n 19 ionice -c3 ... --jobs 1)
"""
import argparse
import datetime
import io
import os
import subprocess
import sys
from concurrent.futures import ProcessPoolExecutor

from PIL import Image, ImageChops

LOSSY_Q = 92
LOSSY_GAIN = 0.70


def encode(path):
    """-> (name, mode, png_bytes, webp_bytes, error)"""
    name = os.path.basename(path)
    try:
        im = Image.open(path)
        im.load()
        rgb = im.convert('RGBA' if 'A' in im.getbands() else 'RGB')
        ll, ly = io.BytesIO(), io.BytesIO()
        rgb.save(ll, 'WEBP', lossless=True, method=6)
        rgb.save(ly, 'WEBP', quality=LOSSY_Q, method=6)
        mode, data = ('lossless', ll) if ly.tell() >= LOSSY_GAIN * ll.tell() else ('lossy', ly)
        data.seek(0)
        back = Image.open(data)
        back.load()
        if back.size != rgb.size:
            return name, mode, 0, 0, 'size mismatch after decode'
        if mode == 'lossless' and ImageChops.difference(back.convert(rgb.mode), rgb).getbbox() is not None:
            return name, mode, 0, 0, 'lossless round trip differs'
        out = path[:-4] + '.webp'
        st = os.stat(path)
        with open(out + '.tmp', 'wb') as f:
            f.write(data.getvalue())
        os.replace(out + '.tmp', out)
        os.utime(out, (st.st_atime, st.st_mtime))
        os.remove(path)
        return name, mode, st.st_size, len(data.getvalue()), None
    except Exception as e:  # keep the PNG, report
        return name, '-', 0, 0, str(e)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('month_dir')
    ap.add_argument('--jobs', type=int, default=os.cpu_count())
    ap.add_argument('--dry-run', action='store_true')
    ap.add_argument('--include-current-month', action='store_true',
                    help="also this month's folder (its files are older than today: the in-tray keeps today's)")
    args = ap.parse_args()

    d = os.path.normpath(args.month_dir)
    base = os.path.basename(d)
    if len(base) != 7 or base[4] != '-' or not base.replace('-', '').isdigit():
        sys.exit('compress-hdmi-archive: %s is not a YYYY-MM archive folder' % d)
    if base == datetime.date.today().strftime('%Y-%m') and not args.include_current_month:
        sys.exit('compress-hdmi-archive: %s is the current month (graders still read its PNGs)' % base)
    tracked = set(subprocess.run(['git', 'ls-files', d], capture_output=True, text=True).stdout.split())
    pngs = sorted(os.path.join(d, f) for f in os.listdir(d) if f.endswith('.png'))
    todo = [p for p in pngs if p not in tracked]
    print('%s: %d PNG, %d git-tracked (kept as PNG), %d to compress' % (d, len(pngs), len(pngs) - len(todo), len(todo)))
    if args.dry_run or not todo:
        return 0
    before = after = 0
    modes = {'lossless': 0, 'lossy': 0}
    errors = []
    with open(os.path.join(d, 'COMPRESSION.tsv'), 'a') as rec, ProcessPoolExecutor(args.jobs) as ex:
        for name, mode, pb, wb, err in ex.map(encode, todo, chunksize=8):
            if err:
                errors.append((name, err))
                continue
            modes[mode] += 1
            before += pb
            after += wb
            rec.write('%s\t%s\t%d\t%d\n' % (name, mode, pb, wb))
    print('compressed %d: %d lossless, %d lossy; %.1f MB -> %.1f MB (%.0f %% saved)' % (
        sum(modes.values()), modes['lossless'], modes['lossy'], before / 1e6, after / 1e6,
        100.0 * (1 - after / before) if before else 0))
    for name, err in errors:
        print('KEPT PNG %s: %s' % (name, err))
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
