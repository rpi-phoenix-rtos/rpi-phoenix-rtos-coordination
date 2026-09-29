#!/usr/bin/env python3
"""Point every HDMI-snapshot citation in the tracked Markdown files at where the file now is.

Snapshots move: scripts/archive-hdmi-snapshots.py files them into artifacts/hdmi/YYYY-MM/, and
scripts/compress-hdmi-archive.py turns an archived .png into a .webp of the same name. A citation
artifacts/hdmi/[YYYY-MM/]<name>.png|.webp that no longer exists is rewritten to the one existing
file with that name stem (in the in-tray or a month folder, .png or .webp). Citations that exist
are left alone; ones that match nothing (snapshots deleted long ago) are listed.

Usage: scripts/relink-hdmi-citations.py [--dry-run]
"""
import glob
import os
import re
import subprocess
import sys

TOP = 'artifacts/hdmi'
CITE = re.compile(r'artifacts/hdmi/(?:20\d\d-\d\d/)?((?:20\d{6}|20\d\d-\d\d-\d\d)-[A-Za-z0-9._-]+?)\.(png|webp)')


def where(stem):
    hits = []
    for ext in ('png', 'webp'):
        hits += glob.glob(os.path.join(TOP, '%s.%s' % (stem, ext)))
        hits += glob.glob(os.path.join(TOP, '20[0-9][0-9]-[0-9][0-9]', '%s.%s' % (stem, ext)))
    return hits


def main():
    dry = '--dry-run' in sys.argv[1:]
    repo = subprocess.run(['git', 'rev-parse', '--show-toplevel'], capture_output=True, text=True).stdout.strip()
    os.chdir(repo)
    files = subprocess.run(['git', 'ls-files', '*.md'], capture_output=True, text=True).stdout.split()
    changed, missing = {}, set()
    for f in files:
        s = open(f).read()

        def rep(m):
            if os.path.exists(m.group(0)):
                return m.group(0)
            hits = where(m.group(1))
            if len(hits) == 1:
                changed[f] = changed.get(f, 0) + 1
                return hits[0]
            missing.add(m.group(0))
            return m.group(0)

        t = CITE.sub(rep, s)
        if t != s and not dry:
            open(f, 'w').write(t)
    for f, n in sorted(changed.items()):
        print('%s %3d  %s' % ('would' if dry else 'fixed', n, f))
    print('%d citation(s) in %d file(s); %d cite snapshots that exist nowhere' % (
        sum(changed.values()), len(changed), len(missing)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
