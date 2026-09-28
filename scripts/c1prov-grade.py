#!/usr/bin/env python3
"""Grade C1 page-provenance dumps (docs/c1-heap-corruption.md, pre-registration `c1prov`).

Usage: c1prov-grade.py <uart-log>...

For each log: drop C1PROV lines whose ck= is not the byte sum mod 0x10000 of the text from
"C1PROV" up to " ck=" (the UART flips ~1.3 % of lines), group lines into dumps (d=<n>.<reason>),
check the per-trial positive control (a reason-0 dump whose newest event is STK's ev=alloc
kind=anon with age < 1000, and a reason-3 ev=oob line), and classify each victim dump
(reason 1 poison break, 2 corrupt header) by the readings fixed before any data:

  VM-BUG         newest event not the dump pid's ev=alloc kind=anon, or header free=1
  PHYS           a phys/physuc/physdev event anywhere in the ring
  HISTORY-LOST   total > depth and only the dump pid's own events remain  (not a reading)
  NO-HISTORY     the page's first event ever is the dump pid's own alloc (total <= depth), or the
                 free before it is kind=none / pid=-1: the writer holds a PA from before the log started
  PREV=<kind>/<who>  the previous owner: the last ev=alloc by ANOTHER owner before the free that
                 precedes the dump pid's first alloc in the ring

Reason-0 dumps are classified the same way and reported as the BASELINE.
"""
import re
import sys
from collections import defaultdict, Counter

LINE = re.compile(r'(C1PROV d=(\d+)\.(\d+) .*?) ck=([0-9a-f]+)')
KV = re.compile(r'(\w+)=(\S+)')


NEED_NOW = ('pa', 'ev', 'pid', 'tick', 'total', 'depth', 'free')
NEED_EV = ('pa', 'ev', 'pid', 'tick', 'kind')
NUM = re.compile(r'^-?(0x)?[0-9a-f]+$')


def well_formed(kv):
    """The byte-sum ck= cannot catch every UART error ('pid=' -> 'pi,8=' keeps the sum), so a
    line must also carry every field its kind needs, with numeric fields that are numbers."""
    need = NEED_NOW if kv.get('ev') == 'now' else NEED_EV
    if kv.get('ev') == 'oob':
        need = ('pa', 'ev')
    if not all(k in kv for k in need):
        return False
    return all(NUM.match(kv[k]) for k in ('pid', 'tick', 'total', 'depth', 'free') if k in kv)


def parse(path):
    raw = open(path, 'rb').read().decode('latin-1')
    raw = re.sub(r'\x1b\[[0-9;]*[A-Za-z]', '', raw)
    dumps = defaultdict(list)
    bad = 0
    for m in LINE.finditer(raw):
        text, d, reason, ck = m.group(1), m.group(2), m.group(3), m.group(4)
        kv = dict(KV.findall(text))
        if sum(text.encode('latin-1')) % 0x10000 != int(ck, 16) or not well_formed(kv):
            bad += 1
            continue
        dumps[(int(d), int(reason))].append(kv)
    return dumps, bad


def owner(e):
    return '%s/%s' % (e.get('kind'), (e.get('who') or 'pid%s' % e.get('pid')).split('/')[-1])


def classify(ev):
    """Returns (class, header, detail). The previous owner is the last ev=alloc before the ev=free
    that precedes the dump pid's CURRENT (newest) alloc of the page (pre-registered rule)."""
    head = [e for e in ev if e.get('ev') == 'now']
    body = [e for e in ev if e.get('ev') not in ('now', 'oob')]
    if any(e.get('ev') == 'oob' for e in ev):
        return 'OOB', None, ''
    if not head:
        return 'NO-HEADER', None, ''
    h = head[0]
    pid, total, depth = h['pid'], int(h['total']), int(h['depth'])
    if h['free'] == '1':
        return 'VM-BUG(free=1)', h, ''
    if not body:
        return 'EMPTY', h, ''
    # seq numbers are the ring's own record: the dump must hold seq max(0, total-depth) .. total-1. A missing one
    # is a line the UART corrupted (dropped above), and a classification from a gapped ring would be false.
    want = set(range(max(0, total - depth), total))
    have = {int(e['seq']) for e in body if 'seq' in e and e['seq'].isdigit()}
    if want - have:
        return 'INCOMPLETE(missing seq %s)' % ','.join(map(str, sorted(want - have))), h, ''
    newest = body[-1]
    if not (newest.get('ev') == 'alloc' and newest.get('kind') == 'anon' and newest.get('pid') == pid):
        return 'VM-BUG(newest=%s)' % owner(newest), h, ''
    foreign = sorted({owner(e) for e in body if e.get('pid') != pid})
    phys = [e for e in body if e.get('ev', '').startswith('phys') or e.get('kind', '').startswith('phys')]
    detail = 'complete' if total <= depth else 'truncated(%d>%d)' % (total, depth)
    detail += ' foreign=' + (','.join(foreign) if foreign else '-')
    if phys:
        return 'PHYS', h, detail
    cur = len(body) - 1
    prev_free = next((i for i in range(cur - 1, -1, -1) if body[i].get('ev') == 'free'), None)
    if prev_free is None:
        return ('NO-HISTORY' if total <= depth else 'HISTORY-LOST'), h, detail
    if body[prev_free].get('kind') == 'none' or body[prev_free].get('pid') == '-1':
        return 'NO-HISTORY', h, detail
    prev_alloc = next((i for i in range(prev_free - 1, -1, -1) if body[i].get('ev') == 'alloc'), None)
    if prev_alloc is None:
        if total > depth:
            return 'HISTORY-LOST', h, detail
        return 'PREV=%s(free-only)' % owner(body[prev_free]), h, detail
    return 'PREV=' + owner(body[prev_alloc]), h, detail


def main():
    victims, baseline = Counter(), Counter()
    for path in sys.argv[1:]:
        dumps, bad = parse(path)
        ctrl0 = any(r == 0 and not classify(ev)[0].startswith(('VM-BUG', 'NO-HEADER', 'EMPTY', 'OOB'))
                    and int(classify(ev)[1]['total']) >= 1 for (d, r), ev in dumps.items())
        ctrl3 = any(r == 3 and any(e.get('ev') == 'oob' for e in ev) for (d, r), ev in dumps.items())
        name = path.rsplit('/', 1)[-1]
        print('== %s  dumps=%d bad_ck=%d control0=%s control3=%s' % (name, len(dumps), bad, ctrl0, ctrl3))
        for (d, r), ev in sorted(dumps.items()):
            cls, h, det = classify(ev)
            pa = h.get('pa') if h else (ev[0].get('pa') if ev else '?')
            tot = h.get('total') if h else '?'
            if r in (1, 2):
                victims[cls] += 1
                tag = 'VICTIM'
            elif r == 0:
                baseline[cls] += 1
                tag = 'baseline'
            else:
                tag = 'control'
            print('   d=%d.%d %-8s pa=%s total=%s -> %-28s %s' % (d, r, tag, pa, tot, cls, det))
    print('SUMMARY victims: %s' % dict(victims))
    print('SUMMARY baseline (reason 0): %s' % dict(baseline))


if __name__ == '__main__':
    main()
