#!/bin/bash
# Verify every issue row in docs/KNOWN-ISSUES.md is a well-formed 3-cell table row.
#
# A markdown table cell is split by any UNESCAPED '|'. Prose and inline code in
# these rows routinely contain one -- `IS_PADDING | padcount`, `current_gen|IS_USED`,
# `(scratch_pa >> 12) | MMU_ILLEGAL_ENABLE` -- and each silently adds a column, so
# the row renders wrong for the owner while looking fine in the raw file. Two rows
# were already broken this way before 2026-09-25 and a third was introduced the
# same day. Escape them as \| .
#
# ⚠ It also has to SEE every row. The first version matched rows with
# `^\| [A-Z]\d+ \|`, which cannot match a struck-through id like `| ~~D6~~ |` -- so a
# row marked RESOLVED sat in this "open only" register, invisible to the check that
# reported "all rows well formed" (2026-09-26, found by the owner). And "exactly 4
# unescaped pipes" alone passed a row with an EXTRA internal pipe and NO trailing
# one (C5). So every table-body line is now a row, it must end with `|`, and a row
# that declares itself resolved is an error: fixed issues belong in the archive.
#
# Usage: scripts/check-known-issues-table.sh [path]   (default docs/KNOWN-ISSUES.md)
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1

python3 - "$@" <<'PY'
import re, sys
path = sys.argv[1] if len(sys.argv) > 1 else 'docs/KNOWN-ISSUES.md'
bad, closed = [], []
rows = 0
for n, raw in enumerate(open(path, encoding='utf-8'), 1):
    line = raw.rstrip('\n')
    # Every table BODY line is a row: header ('| # |') and separator ('|---') aside.
    if not line.startswith('|') or line.startswith('| # |') or re.match(r'^\|\s*-', line):
        continue
    rows += 1
    ident = re.split(r'(?<!\\)\|', line)[1].strip() if line.count('|') > 1 else line[:8]
    pipes = [m.start() for m in re.finditer(r'(?<!\\)\|', line)]
    if len(pipes) != 4 or not line.rstrip().endswith('|'):
        why = 'has %d unescaped pipes' % len(pipes) if len(pipes) != 4 else 'does not end with |'
        bad.append((n, ident, why))
    # A row that says it is fixed does not belong in an open-issues register.
    cells = [c.strip() for c in re.split(r'(?<!\\)\|', line)[1:-1]]
    struck = ident.startswith('~~')
    declared = any(re.match(r'^(\u2705\s*)?\*{0,2}(RESOLVED|CLOSED|FIXED)\b', c) for c in cells[1:])
    if struck or declared:
        closed.append((n, ident, 'struck-through id' if struck else 'a cell opens with RESOLVED/CLOSED/FIXED'))
print('issue rows checked: %d' % rows)
rc = 0
if bad:
    rc = 1
    print('MALFORMED (a 3-cell row needs exactly 4 unescaped pipes and must end with |):')
    for n, ident, why in bad:
        print('  line %-5d %-10s %s -- escape extra pipes as \\|' % (n, ident, why))
if closed:
    rc = 1
    print('CLOSED ISSUE STILL IN THE OPEN REGISTER (move it to docs/done/closed-issues-archive.md):')
    for n, ident, why in closed:
        print('  line %-5d %-10s %s' % (n, ident, why))
if rc == 0:
    print('all rows well formed, none closed')
sys.exit(rc)
PY
