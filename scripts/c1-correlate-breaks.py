#!/usr/bin/env python3
"""Correlate C1 page-poison breaks against heaps the trace saw CREATED.

Why this exists: malloc_c1P4Poison has exactly one call site, _malloc_chunkAdd,
which runs BOTH when a chunk is freed AND when a brand-new heap's chunk is
binned. So a poison break on its own cannot distinguish

  (a) a heap the application finished with, written afterwards, from
  (b) a heap just mmap'd onto pages some device still writes to.

(b) is the leading model: it is what V3D_KEEP_CLOSED_BO=1 suppresses, and
malloc_dl itself notes that a new heap region may be one released earlier
because mmap reuses addresses.

The distinction is free once C1_HEAP_TRACE=1 is armed: the trace logs c1base +
c1size for every traced heap CREATION, and a break logs p4page. A break inside
[c1base, c1base+c1size) of a heap created earlier in the SAME run means the
victim was freshly created -- story (b). Never inside means story (a).

Usage: python3 scripts/c1-correlate-breaks.py <log>...

Validated both ways 2026-09-25 on a synthetic log: reports inside=1 for a break
placed inside a traced heap, inside=0 with the same break moved outside it.
"""
import sys
import re


def analyse(path):
    lines = open(path, 'rb').read().decode('utf-8', 'replace').split('\n')

    heaps = []
    for i, l in enumerate(lines):
        if 'C1-hunt: created' not in l:
            continue
        blk = '\n'.join(lines[i:i + 6])
        base = re.search(r'c1base = 0x([0-9a-f]+)', blk)
        size = re.search(r'c1size = 0x([0-9a-f]+)', blk)
        if base:
            # Pre-2026-09-25 builds traced 0xd000 only and printed no c1size.
            heaps.append((int(base.group(1), 16),
                          int(size.group(1), 16) if size else 0xd000))

    breaks = []
    for i, l in enumerate(lines):
        if 'POISON BROKEN' not in l:
            continue
        blk = '\n'.join(lines[i:i + 12])
        page = re.search(r'p4page = 0x([0-9a-f]+)', blk)
        if page:
            breaks.append(int(page.group(1), 16))

    inside = [b for b in breaks if any(h <= b < h + s for h, s in heaps)]
    sizes = {}
    for _, s in heaps:
        sizes[hex(s)] = sizes.get(hex(s), 0) + 1

    print('%-38s heaps=%-3d %s' % (path.split('/')[-1][10:48], len(heaps), sizes))
    print('%-38s breaks=%-3d inside a traced heap=%d' % ('', len(breaks), len(inside)))
    for b in inside:
        print('%-38s   break page 0x%x IS inside a heap created this run' % ('', b))


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    for p in sys.argv[1:]:
        analyse(p)
