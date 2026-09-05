#!/usr/bin/env bash
#
# diff-sdimage-vs-export.sh — prove the SD image and the NFS-root export carry
# the SAME binaries.
#
# WHY: owner chain item 11 ("compare the key binaries: new NFS rootfs vs new SD
# image"). Both are supposed to be two packagings of one build tree
# (_fs/<target>/root). If they diverge, one of them was hand-staged or a cut was
# taken at a different moment, and a Pi test on one says nothing about the other
# -- exactly the failure mode that made an SD boot and a netboot print different
# diagnostics.
#
# Compares every ELF that exists in BOTH roots by sha256, and lists what exists
# in only one. Read-only: the image is loop-mounted -o ro.
#
#   ./scripts/diff-sdimage-vs-export.sh [image] [export-dir]
#
# Defaults: the newest artifacts/rpi4b/*.img, and the fsid=0 export.
# Exit 0 when every shared ELF matches, 1 when any differ, 2 on setup trouble.
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

img="${1:-}"
if [ -z "${img}" ]; then
	img="$(ls -t "${ROOT}"/artifacts/rpi4b/rpi4b-sd-2part*.img 2>/dev/null | head -1)"
fi
[ -f "${img}" ] || { printf 'diff-sdimage-vs-export: no image (%s)\n' "${img}" >&2; exit 2; }

exp="${2:-}"
if [ -z "${exp}" ]; then
	exp="$(awk '$0 ~ /fsid=0/ && $1 ~ /^\// { print $1; exit }' /etc/exports /etc/exports.d/*.exports 2>/dev/null || true)"
fi
[ -d "${exp}" ] || { printf 'diff-sdimage-vs-export: no export dir (%s)\n' "${exp}" >&2; exit 2; }

# The rootfs is partition 2; take its offset from the partition table rather than
# assuming the historical 135168 sectors.
off="$(sfdisk -J "${img}" 2>/dev/null | python3 -c '
import json,sys
d=json.load(sys.stdin)["partitiontable"]
parts=d["partitions"]
sec=d.get("sectorsize",512)
print(parts[1]["start"]*sec)
')"
[ -n "${off}" ] || { printf 'diff-sdimage-vs-export: could not read the partition table\n' >&2; exit 2; }

mnt="$(mktemp -d)"
cleanup() { sudo umount "${mnt}" 2>/dev/null; rmdir "${mnt}" 2>/dev/null; }
trap cleanup EXIT

sudo mount -o ro,loop,offset="${off}" "${img}" "${mnt}" ||
	{ printf 'diff-sdimage-vs-export: mount failed\n' >&2; exit 2; }

printf '== SD image  : %s (rootfs @ %s)\n== export    : %s\n\n' "${img}" "${off}" "${exp}"

python3 - "${mnt}" "${exp}" <<'PY'
import hashlib, os, sys

def elfs(root):
    out = {}
    for dp, _, fs in os.walk(root):
        for f in fs:
            p = os.path.join(dp, f)
            if os.path.islink(p) or not os.path.isfile(p):
                continue
            try:
                with open(p, 'rb') as fh:
                    if fh.read(4) != b'\x7fELF':
                        continue
                    fh.seek(0)
                    h = hashlib.sha256(fh.read()).hexdigest()
            except OSError:
                continue
            out[p[len(root):]] = h
    return out

a, b = elfs(sys.argv[1]), elfs(sys.argv[2])
shared = sorted(set(a) & set(b))
differ = [p for p in shared if a[p] != b[p]]
only_img = sorted(set(a) - set(b))
only_exp = sorted(set(b) - set(a))

print(f'ELFs in image : {len(a)}')
print(f'ELFs in export: {len(b)}')
print(f'in both       : {len(shared)}  -> identical {len(shared)-len(differ)}, DIFFERING {len(differ)}')
for p in differ:
    print(f'  DIFFER  {p}')
if only_img:
    print(f'\nonly in the image ({len(only_img)}):')
    for p in only_img:
        print(f'  {p}')
if only_exp:
    print(f'\nonly in the export ({len(only_exp)}):')
    for p in only_exp:
        print(f'  {p}')
print()
if differ:
    print('RESULT: the two roots carry DIFFERENT builds of the binaries above.')
else:
    print('RESULT: every shared ELF is byte-identical — one build, two packagings.')
sys.exit(1 if differ else 0)
PY
