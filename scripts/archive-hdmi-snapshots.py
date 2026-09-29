#!/usr/bin/env python3
"""Move HDMI snapshots into one sub-folder per month: artifacts/hdmi/YYYY-MM/.

The capture scripts write every snapshot to artifacts/hdmi/ (the in-tray); this files the older
ones away by the date in their name, so the folder stays navigable and the history is kept.

  <YYYYMMDD>-[<HHMMSS>-]<label>....png -> artifacts/hdmi/YYYY-MM/
  <YYYY-MM-DD>-....png                  -> artifacts/hdmi/YYYY-MM/   (older naming)

Files dated TODAY stay in the in-tray (a running cycle or gate may still be grading them), unless
--include-today. Undated files and existing sub-folders are left alone. A file whose name already
exists in the month folder is reported and not moved. The readers (check-torch-rois.py,
compare-boots.py, stk-fps-from-hdmi.py, check-hdmi-content.py, the showcase gates) look in the
in-tray and in the month folders.

Usage: scripts/archive-hdmi-snapshots.py [--dry-run] [--include-today] [--dir artifacts/hdmi]
"""
import argparse
import datetime
import os
import re
import sys

STAMP = re.compile(r'^(20\d\d)(\d\d)(\d\d)-')
DASHED = re.compile(r'^(20\d\d)-(\d\d)-(\d\d)-')


def month_of(name):
    m = STAMP.match(name) or DASHED.match(name)
    if not m:
        return None, None
    y, mo, d = m.groups()
    if not (1 <= int(mo) <= 12 and 1 <= int(d) <= 31):
        return None, None
    return '%s-%s' % (y, mo), '%s%s%s' % (y, mo, d)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--dir', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'artifacts', 'hdmi'))
    ap.add_argument('--dry-run', action='store_true')
    ap.add_argument('--include-today', action='store_true')
    args = ap.parse_args()

    top = os.path.normpath(args.dir)
    today = datetime.date.today().strftime('%Y%m%d')
    moved, kept_today, undated, clash = {}, 0, 0, 0
    with os.scandir(top) as it:
        entries = [e for e in it if e.is_file(follow_symlinks=False)]
    for e in entries:
        month, day = month_of(e.name)
        if month is None:
            undated += 1
            continue
        if day == today and not args.include_today:
            kept_today += 1
            continue
        dest_dir = os.path.join(top, month)
        dest = os.path.join(dest_dir, e.name)
        if os.path.exists(dest):
            print('CLASH   %s: already in %s/, left in place' % (e.name, month))
            clash += 1
            continue
        if not args.dry_run:
            os.makedirs(dest_dir, exist_ok=True)
            os.rename(e.path, dest)
        moved[month] = moved.get(month, 0) + 1
    verb = 'would move' if args.dry_run else 'moved'
    for month in sorted(moved):
        print('%s %6d -> %s/' % (verb, moved[month], month))
    print('left in %s/: %d dated today, %d undated, %d clashes' % (os.path.basename(top), kept_today, undated, clash))
    return 1 if clash else 0


if __name__ == '__main__':
    sys.exit(main())
