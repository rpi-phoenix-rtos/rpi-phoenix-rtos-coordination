# libscanf-hosttest — differential harness for libphoenix's sscanf

Compiles libphoenix's real `stdio/scanf.c` natively and compares `sscanf` against the host
glibc — return value, every parsed value, and the `%n` position — over 1564 comparisons of
integer, float, string and scanset conversions. Runs instantly, no Pi cycle.

```
make run      # expect: diffs=0 known=87
```

The numeric leaves (`strtod`, `strtoll`, …) are deliberately left bound to glibc, so a
difference here is scanf's **own** parsing rather than a conversion routine that
`tools/libnum-hosttest/` already covers.

## What it found (2026-09-25)

**A defect cluster in the suppressed-conversion path, now fixed.** `scanf.c` recorded how much
input an integer conversion had consumed as `p - buf` — the count of characters *copied for
conversion*. A suppressed (`%*`) conversion copies nothing, deliberately, since that is what
lets its width run unbounded. So `p - buf` was zero and three things broke:

* `%n` came out short by exactly the suppressed digits — `sscanf("12 34", "%*d%d%n", &v, &n)`
  set `n = 3` where glibc sets `5`.
* the `0x` prefix test, `p == buf + 1`, could never fire under suppression, so `%*i` and
  `%*x` could not recognise a hex prefix.
* `c = ((unsigned char *)p)[-1]` read **one byte before `buf`** whenever nothing had been
  copied — which under `SUPPRESS` is always. If that stack garbage happened to be `'x'` or
  `'X'`, the code then did `--p; inp--; (*inr)++`, un-reading a character it had never read.

Also a missing `<stddef.h>` (`ptrdiff_t`, used by `%t`), invisible to the target build because
another header pulls it in there.

## The 87 classified divergences — measured, understood, NOT changed

**EOF vs 0 (84).** `sscanf("-", "%d", &v)` gives `-1` here and `0` in glibc; `sscanf("42",
"%*d%d", &v)` gives `0` here and `-1` in glibc. C17 7.21.6.2 returns EOF only when an input
failure occurs "before the first conversion (if any) has completed", and whether a
*suppressed* conversion counts as completing is genuinely ambiguous — Phoenix's reading of the
`%*d` case is arguably the more literal one. Changing scanf's return semantics on a contested
reading would risk every port for no clear correctness gain.

**C23 `0b` prefix (3).** A glibc extension C17 does not require; classified the same way in
`tools/libnum-hosttest/`.

**⚠ An incomplete float item is accepted (the rest) — a REAL defect, left for its own change.**
C17 7.21.6.2p12-13: the input item is the longest sequence that *is, or is a prefix of,* a
matching sequence, and **if that item is not itself a matching sequence the directive fails**.
`"1e"` is only a prefix, so scanf must fail — but Phoenix converts the valid sub-prefix and
reports success:

| input | Phoenix | glibc |
|---|---|---|
| `1e` | `rc=1, v=1` | `rc=0` |
| `1e+` | `rc=1, v=1` | `rc=0` |
| `1.5e` | `rc=1, v=1.5` | `rc=0` |
| `0x` | `rc=1, v=0` | `rc=0` |

So `sscanf(s, "%lf", &d) == 1` accepts malformed input. This is *not* a `strtod` bug —
`strtod("1e")` correctly returns 1 with `endptr` at offset 1, because strtod takes the longest
valid **prefix** while scanf must reject an item that is only a prefix. Fixing it means
scanning greedily per the float grammar and then requiring `strtod` to consume the whole item,
which is a restructure of the `CT_FLOAT` case rather than a patch — recorded here rather than
guessed at.

Sibling harnesses: `tools/libfmt-hosttest/`, `tools/libnum-hosttest/`, `tools/libtime-hosttest/`,
`tools/libstring-hosttest/`, `tools/libwchar-hosttest/`, `tools/libext2-hosttest/`.
