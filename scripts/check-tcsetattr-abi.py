#!/usr/bin/env python3
#
# check-tcsetattr-abi.py -- census of aarch64 ELFs by the libphoenix tcsetattr()
# they carry, i.e. whether they were linked before or after libphoenix 2b6b552.
#
#   scripts/check-tcsetattr-abi.py /srv/phoenix-rpi4-nfs-gcc16              # bin usr/bin sbin usr/sbin
#   scripts/check-tcsetattr-abi.py .buildroot/_build/aarch64a72-generic-rpi4b prog prog.stripped
#
# OLD (before 2b6b552) builds TCSETS in a W register and sign-extends it, so the
# request reaches libtty as 0xffffffff805c7402 and, since devices 1657d7d, fails
# with EINVAL (the xterm "fatal pty error errno=22", docs/misc/2026-09-27-xterm-pty-einval.md):
#   mov w0,#0x7402; movk w0,#0x805c,lsl #16; add w1,w1,w0; ...; sxtw xN,w1
# NEW builds it in an X register:
#   mov x0,#0x7402; movk x0,#0x805c,lsl #16; add x20,x0,w1,sxtw
#
# Matched on the raw instruction bytes, so STRIPPED binaries are classified too
# (the objdump --disassemble=tcsetattr method needs symbols and missed picocom,
# openssl and wpa_supplicant). A binary is a marker of its whole libc: OLD here
# means linked against a libphoenix older than 2026-09-21.
#
# Exit 0 when no OLD binary is found, 1 otherwise.
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause
"""Census of aarch64 ELFs by their libphoenix tcsetattr() build (see header)."""
import os, struct, sys
OLD = struct.pack("<2I", 0x528e8040, 0x72b00b80)   # TCSETS built in a W register
NEW = struct.pack("<2I", 0xd28e8040, 0xf2b00b80)   # TCSETS built in an X register


def is_old(data):
    """w-form TCSETS followed, within 8 instructions, by `sxtw xN, wM` (the
    compiler schedules it differently across builds)."""
    i = data.find(OLD)
    while i >= 0:
        for j in range(i + 8, min(i + 8 + 32, len(data) - 3), 4):
            (w,) = struct.unpack_from("<I", data, j)
            if w & 0xfffffc00 == 0x93407c00:
                return True
        i = data.find(OLD, i + 4)
    return False
root = sys.argv[1]
dirs = sys.argv[2:] or ["bin", "usr/bin", "sbin", "usr/sbin"]
res = {"OLD": [], "NEW": [], "none": []}
seen = set()
for d in dirs:
    for dp, _, fs in os.walk(os.path.join(root, d)):
        for f in sorted(fs):
            p = os.path.join(dp, f)
            if os.path.islink(p) or not os.path.isfile(p):
                continue
            rp = os.path.realpath(p)
            if rp in seen:
                continue
            seen.add(rp)
            with open(p, "rb") as fh:
                data = fh.read()
            if data[:4] != b"\x7fELF" or data[18:20] != b"\xb7\x00":
                continue
            k = "OLD" if is_old(data) else "NEW" if NEW in data else "none"
            res[k].append(os.path.relpath(p, root))
for k in ("OLD",):
    print(f"{k} ({len(res[k])}):")
    for x in res[k]:
        print("  " + x)
print(f"NEW: {len(res['NEW'])}   no tcsetattr linked: {len(res['none'])}")
sys.exit(1 if res["OLD"] else 0)
