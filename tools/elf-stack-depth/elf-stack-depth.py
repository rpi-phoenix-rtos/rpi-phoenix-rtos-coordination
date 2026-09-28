#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""
elf-stack-depth.py -- worst-case user-stack depth of a thread entry, from a
linked AArch64 ELF, checked against the stack that thread is given.

Why: a thread that runs on a small static stack (lwip's collector thread ran on
512 bytes) overflows DOWNWARDS into whatever the linker placed below that array,
silently: the kernel's user-stack canary check is compiled out under NDEBUG.
A libc change that grows one frame by 16 bytes is enough (see
docs/misc/2026-09-28-lwip-route-find-crash.md). This finds that on the host, in
about a second, from the image that will actually boot.

Method (static, conservative for direct calls):
  * function starts = symbols (if the ELF is unstripped) + every `bl` target;
  * frame(f) = the prologue's stack decrements: pre-indexed `st*`/`stp`
    with a negative writeback on sp, plus `sub sp, sp, #imm`; a
    `sub sp, sp, <reg>` (alloca/VLA) marks the frame UNBOUNDED;
  * depth(f) = max(frame(f) + depth(bl callee), depth(tail-called b target));
  * `blr` (indirect) calls are NOT followed -- they are counted on the path;
  * recursion is cut at the first repeat and reported.
AArch64 has no red zone, so the deepest store is at (top - depth). The initial
SP is the stack top rounded down to 16 (hal_cpuCreateContext).

Usage:
  elf-stack-depth.py ELF --root SYM_OR_ADDR:STACKBYTES [--root ...]
                     [--path-to SYM_OR_ADDR] [--reserve N] [--objdump PATH]
                     [--names-from UNSTRIPPED_REF_ELF]

  A stripped ELF (e.g. one cut out of loader.disk) has no symbols, so function
  bodies can only be bounded by `bl` targets and over-approximate; --names-from
  borrows the function starts and names from an unstripped build of nearly the
  same source (verify the frames you quote against the disassembly).

Exit status: 0 if every root has margin >= --reserve, 1 otherwise, 2 on error.
"""

import argparse
import bisect
import functools
import glob
import os
import re
import subprocess
import sys

BRANCH_MN = ('bl', 'b')


def find_tool(name, override):
    if override:
        return override
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.abspath(os.path.join(here, '..', '..'))
    hits = sorted(glob.glob(os.path.join(root, '.toolchain', '*', 'bin', 'aarch64-phoenix-' + name)))
    if not hits:
        sys.exit('elf-stack-depth: cannot find aarch64-phoenix-%s under .toolchain/*/bin; pass --objdump' % name)
    return hits[0]


class Elf:
    def __init__(self, path, objdump):
        self.path = path
        dis = subprocess.run([objdump, '-d', '--no-show-raw-insn', path], check=True,
                             capture_output=True, text=True).stdout
        self.ins = {}
        self.names = {}
        for line in dis.splitlines():
            m = re.match(r'([0-9a-f]+) <([^>+]+)>:$', line)
            if m:
                self.names[int(m.group(1), 16)] = m.group(2)
                continue
            m = re.match(r'\s+([0-9a-f]+):\s+(\S+)\s*(.*)', line)
            if m:
                self.ins[int(m.group(1), 16)] = (m.group(2), m.group(3))
        self.addrs = sorted(self.ins)
        starts = set(a for a, n in self.names.items() if a in self.ins and not n.startswith('.'))
        for a in self.addrs:
            mn, op = self.ins[a]
            if mn == 'bl':
                t = self.target(op)
                if t is not None:
                    starts.add(t)
        self.starts = sorted(starts)
        self.byname = {n: a for a, n in self.names.items()}

    def norm(self, a):
        mn, op = self.ins[a]
        if mn in ('bl', 'b', 'adrp', 'adr', 'cbz', 'cbnz', 'tbz', 'tbnz') or mn.startswith('b.'):
            op = re.sub(r'(?:0x)?[0-9a-f]{4,}\b.*', '', op)
        op = re.sub(r'//.*', '', op).strip()
        if mn not in ('stp', 'ldp', 'sub') or 'sp' not in op:
            op = re.sub(r'#(?:0x)?[0-9a-f]+', '#', op)   # data offsets move between builds
        return mn + ' ' + op

    def adopt_names(self, ref, k=8):
        """Name a stripped ELF's functions from an unstripped build of (nearly) the same
        source: each ref function's first k instructions, relocations masked, are looked for
        in this ELF, and the match nearest the running address delta wins."""
        seq = [self.norm(a) for a in self.addrs]
        index = {}
        for i in range(len(seq) - k):
            index.setdefault(tuple(seq[i:i + k]), []).append(i)
        delta, adopted = 0, 0
        for ra in sorted(a for a in ref.names if a in ref.ins and not ref.names[a].startswith('.')):
            ri = bisect.bisect_left(ref.addrs, ra)
            key = tuple(ref.norm(x) for x in ref.addrs[ri:ri + k])
            cands = index.get(key, [])
            if not cands:
                continue
            best = min(cands, key=lambda i: abs(self.addrs[i] - (ra + delta)))
            ta = self.addrs[best]
            if abs(ta - (ra + delta)) > 0x1000:
                continue
            delta = ta - ra
            self.names[ta] = ref.names[ra]
            adopted += 1
        self.starts = sorted(set(self.starts) | set(a for a in self.names if a in self.ins))
        self.byname = {n: a for a, n in self.names.items()}
        self.frame.cache_clear()
        self.edges.cache_clear()
        return adopted

    @staticmethod
    def target(op):
        m = re.match(r'(?:0x)?([0-9a-f]+)\b', op)
        return int(m.group(1), 16) if m else None

    def resolve(self, s):
        if s in self.byname:
            return self.byname[s]
        try:
            return int(s, 16)
        except ValueError:
            sys.exit('elf-stack-depth: no symbol %r in %s (stripped? pass an address)' % (s, self.path))

    def name(self, a):
        return self.names.get(a, '%#x' % a)

    def body(self, s):
        i = bisect.bisect_left(self.addrs, s)
        j = bisect.bisect_right(self.starts, s)
        end = self.starts[j] if j < len(self.starts) else self.addrs[-1] + 4
        return self.addrs[i:bisect.bisect_left(self.addrs, end)]

    @functools.lru_cache(None)
    def frame(self, s):
        tot, unbounded = 0, False
        for a in self.body(s)[:24]:
            mn, op = self.ins[a]
            g = re.search(r'\[sp, #-(\d+)\]!', op)
            if mn.startswith('st') and g:
                tot += int(g.group(1))
            g = re.match(r'sp, sp, #(0x[0-9a-f]+|\d+)(, lsl #12)?$', op)
            if mn == 'sub' and g:
                tot += int(g.group(1), 0) * (4096 if g.group(2) else 1)
            elif mn == 'sub' and re.match(r'sp, sp, [xw]\d+', op):
                unbounded = True
        return tot, unbounded

    @functools.lru_cache(None)
    def edges(self, s):
        body = self.body(s)
        lo, hi = (body[0], body[-1]) if body else (s, s)
        calls, tails, indirect = [], [], 0
        for a in body:
            mn, op = self.ins[a]
            if mn == 'blr':
                indirect += 1
            elif mn in BRANCH_MN:
                t = self.target(op)
                if t is None:
                    continue
                if mn == 'bl':
                    calls.append(t)
                elif (t < lo or t > hi) and t in self.starts:
                    tails.append(t)
        return tuple(calls), tuple(tails), indirect


def worst(elf, root, goal=None):
    """Deepest path from root (to goal if given): (depth, [(func, frame)], notes)."""
    notes = set()

    memo, visiting = {}, set()

    # Memoised per function, with cycles cut at the first repeat on the current DFS
    # path; exact for an acyclic call graph, and recursion is reported, not sized.
    def walk(s):
        if s in memo:
            return memo[s]
        fr, unb = elf.frame(s)
        if unb:
            notes.add('UNBOUNDED frame (sub sp, sp, reg) in %s' % elf.name(s))
        calls, tails, indirect = elf.edges(s)
        if indirect:
            notes.add('%d indirect call(s) not followed in %s' % (indirect, elf.name(s)))
        if goal is not None and s == goal:
            memo[s] = (fr, ((s, fr),))
            return memo[s]
        best = None if goal is not None else (fr, ((s, fr),))
        visiting.add(s)
        for c in set(calls):
            if c in visiting:
                notes.add('recursion cut at %s -> %s' % (elf.name(s), elf.name(c)))
                continue
            r = walk(c)
            if r and (best is None or fr + r[0] > best[0]):
                best = (fr + r[0], ((s, fr),) + r[1])
        for t in set(tails):
            if t in visiting:
                continue
            r = walk(t)   # a tail call replaces this frame
            if r and (best is None or r[0] > best[0]):
                best = (r[0], ((s, 0),) + r[1])
        visiting.discard(s)
        memo[s] = best
        return best

    r = walk(root)
    return (r[0], list(r[1]), sorted(notes)) if r else (None, [], sorted(notes))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('elf')
    ap.add_argument('--root', action='append', required=True, help='SYM_OR_ADDR:STACKBYTES')
    ap.add_argument('--path-to', help='also print the deepest path from each root to this function')
    ap.add_argument('--reserve', type=int, default=0, help='required spare bytes per root (default 0)')
    ap.add_argument('--names-from', metavar='REF_ELF',
                    help='name a stripped ELF from an unstripped build of nearly the same source')
    ap.add_argument('--objdump')
    a = ap.parse_args()

    objdump = find_tool('objdump', a.objdump)
    elf = Elf(a.elf, objdump)
    if a.names_from:
        n = elf.adopt_names(Elf(a.names_from, objdump))
        print('named %d functions of %s from %s' % (n, a.elf, a.names_from))
    sys.setrecursionlimit(20000)
    ok = True
    for spec in a.root:
        sym, _, size = spec.rpartition(':')
        root, size = elf.resolve(sym), int(size, 0)
        depth, path, notes = worst(elf, root)
        margin = size - depth
        verdict = 'OK' if margin >= a.reserve else 'FAIL'
        ok &= verdict == 'OK'
        print('%s: %s stack=%d worst=%d margin=%d' % (verdict, elf.name(root), size, depth, margin))
        print('  worst path: ' + ' -> '.join('%s(%d)' % (elf.name(f), fr) for f, fr in path))
        if a.path_to:
            goal = elf.resolve(a.path_to)
            gd, gp, _ = worst(elf, root, goal)
            if gd is None:
                print('  no direct-call path to %s' % elf.name(goal))
            else:
                print('  deepest path to %s: %d bytes, margin %d: %s' % (
                    elf.name(goal), gd, size - gd,
                    ' -> '.join('%s(%d)' % (elf.name(f), fr) for f, fr in gp)))
        for n in notes:
            print('  note: ' + n)
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
