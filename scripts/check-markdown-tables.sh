#!/usr/bin/env bash
# Validate markdown tables in one or more files.
#
# Why this exists: docs/KNOWN-ISSUES.md was silently broken TWICE by edits that
# look fine in a diff and render fine locally, but not on GitHub:
#
#   1. A BLANK LINE inside a table ends it. Every row after the blank becomes a
#      second table with no header, which GitHub renders as plain paragraph
#      text. The symptom is "the table stops being a table half way down", and
#      the diff that caused it touches a completely different row.
#   2. Appending text AFTER a row's trailing `|` adds a fourth cell to a
#      three-column table. GitHub then drops or misaligns the extra content.
#
# Both are invisible unless you count delimiters, so count them here.
#
# Usage: ./scripts/check-markdown-tables.sh [file.md ...]
#        (default: docs/KNOWN-ISSUES.md)
# Exit 0 = clean, 1 = problems found (reported with line numbers).

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
files=("$@")
if [ ${#files[@]} -eq 0 ]; then
	files=("${repo_root}/docs/KNOWN-ISSUES.md")
fi

python3 - "${files[@]}" <<'PYEOF'
import re, sys


def cells(s):
    """Count cell separators, ignoring BACKSLASH-ESCAPED pipes.

    `\\|` is a literal pipe in GitHub-flavoured markdown, not a separator, and
    it is the normal way to write `A|B` inside a cell. Counting raw '|' flags
    every such row as broken -- a false alarm that would train the reader to
    ignore this check, which is worse than not having it.
    """
    return len(re.findall(r'(?<!\\)\|', s)) - 1


rc = 0
for path in sys.argv[1:]:
    try:
        lines = open(path, encoding='utf8').read().split('\n')
    except OSError as e:
        print("ERROR: %s: %s" % (path, e))
        rc = 1
        continue

    problems = []
    in_table = False
    header_cells = 0
    header_line = 0
    tables = 0
    blank_run_start = None

    for n, s in enumerate(lines, 1):
        is_row = s.startswith('|')

        if is_row and not in_table:
            in_table = True
            tables += 1
            header_cells = cells(s)
            header_line = n
            blank_run_start = None
            continue

        if in_table and not is_row:
            # A blank line legitimately ends a table -- but if a row follows
            # after only blank lines, the author almost certainly meant it to
            # stay one table.
            if s.strip() == '':
                if blank_run_start is None:
                    blank_run_start = n
                continue
            in_table = False
            blank_run_start = None
            continue

        if in_table and is_row:
            if blank_run_start is not None:
                # A blank line followed by a row that is ITSELF a new header --
                # i.e. the next line is a |---|---| delimiter -- is two adjacent
                # tables, which is perfectly legal. Only a blank followed by a
                # bare continuation row is the bug.
                nxt = lines[n] if n < len(lines) else ''
                if re.match(r'^\|[-\s|:]+\|$', nxt):
                    in_table = True
                    tables += 1
                    header_cells = cells(s)
                    header_line = n
                    blank_run_start = None
                    continue
                problems.append((blank_run_start,
                    "blank line inside a table (started at header line %d) -- "
                    "rows below it render as plain text on GitHub" % header_line))
                blank_run_start = None
            # the |---|---| delimiter row is free-form
            if re.match(r'^\|[-\s|:]+\|$', s):
                continue
            # A row that lost its trailing `|` is the exact shape produced by
            # appending after the last cell, and it has the SAME pipe count as a
            # well-formed row -- so test the delimiter itself, not just the
            # count, or the check silently passes the defect it exists to catch.
            if not s.rstrip().endswith('|'):
                problems.append((n,
                    "row does not end with '|' -- text was appended after the "
                    "last cell, which adds a column: %s" % s[-60:]))
                continue
            n_cells = cells(s)
            if n_cells != header_cells:
                problems.append((n,
                    "row has %d cells, header at line %d has %d: %s"
                    % (n_cells, header_line, header_cells, s[:60])))

    if problems:
        rc = 1
        print("%s: %d problem(s)" % (path, len(problems)))
        for n, msg in sorted(problems):
            print("  line %d: %s" % (n, msg))
    else:
        print("%s: OK (%d table(s))" % (path, tables))

sys.exit(rc)
PYEOF
