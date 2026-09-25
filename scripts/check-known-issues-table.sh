#!/bin/bash
# Verify every issue row in docs/KNOWN-ISSUES.md is a well-formed 3-cell table row.
#
# A markdown table cell is split by any UNESCAPED '|'. Prose and inline code in
# these rows routinely contain one -- `IS_PADDING | padcount`, `current_gen|IS_USED`,
# `(scratch_pa >> 12) | MMU_ILLEGAL_ENABLE` -- and each silently adds a column, so
# the row renders wrong for the owner while looking fine in the raw file. Two rows
# were already broken this way before 2026-09-25 and a third was introduced the
# same day. Escape them as \| .
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1

python3 - "$@" <<'PY'
import re, sys
path = 'docs/KNOWN-ISSUES.md'
bad = []
rows = 0
for n, line in enumerate(open(path, encoding='utf-8'), 1):
    if not re.match(r'^\| [A-Z]\d+ \|', line):
        continue
    rows += 1
    pipes = [m.start() for m in re.finditer(r'(?<!\\)\|', line)]
    if len(pipes) != 4:
        bad.append((n, line[:7].strip(), len(pipes)))
print('issue rows checked: %d' % rows)
if bad:
    print('MALFORMED (a 3-cell row needs exactly 4 unescaped pipes):')
    for n, ident, c in bad:
        print('  line %-5d %-6s has %d -- escape the extra ones as \\|' % (n, ident, c))
    sys.exit(1)
print('all rows well formed')
PY
