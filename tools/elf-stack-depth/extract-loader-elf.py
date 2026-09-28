#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""
extract-loader-elf.py -- cut the (stripped) program ELFs out of a loader.disk.

Why: .buildroot/_build/<target>/prog/<name> is overwritten by the NEXT build, so
after a rebuild it no longer matches the image that crashed. The image itself
(loader.disk, or an archived copy) still holds every syspage program; this
recovers them for objdump/addr2line-by-matching (elf-stack-depth.py --names-from).

Usage: extract-loader-elf.py LOADER_DISK OUTDIR [--needle TEXT ...]
Writes OUTDIR/elf<N>.bin for every ELF found and prints offset, size, and which
--needle strings each contains (e.g. --needle genet --needle _route_find).
"""

import argparse
import hashlib
import os
import struct
import sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('disk')
    ap.add_argument('outdir')
    ap.add_argument('--needle', action='append', default=[])
    a = ap.parse_args()

    d = open(a.disk, 'rb').read()
    os.makedirs(a.outdir, exist_ok=True)
    off, n = 0, 0
    while True:
        o = d.find(b'\x7fELF\x02\x01\x01', off)
        if o < 0:
            break
        off = o + 1
        shoff, = struct.unpack_from('<Q', d, o + 0x28)
        shentsize, shnum = struct.unpack_from('<HH', d, o + 0x3a)
        end = shoff + shentsize * shnum
        if shentsize != 64 or not 0 < end <= len(d) - o:
            continue     # an ELF magic inside another file's data, not a header
        blob = d[o:o + end]
        path = os.path.join(a.outdir, 'elf%d.bin' % n)
        open(path, 'wb').write(blob)
        hits = [s for s in a.needle if s.encode() in blob]
        print('%s off=%d size=%d sha256=%s %s' % (path, o, end, hashlib.sha256(blob).hexdigest()[:16],
                                                  ' '.join('has:' + h for h in hits)))
        n += 1
    return 0 if n else 1


if __name__ == '__main__':
    sys.exit(main())
