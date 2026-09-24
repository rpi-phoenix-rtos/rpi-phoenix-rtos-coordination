# libnum-hosttest — differential harness for libphoenix's numeric conversions

Compiles libphoenix's real `stdlib/strtoul.c`, `strtoull.c` and `strtod.c` natively and
compares **value, end pointer and the ERANGE decision** against the host glibc over ~3200
cases, in about a second. No Pi cycle, no cross-build.

`strtod`/`strtof` are compared **bit-exactly** on their IEEE-754 payload, not with a
tolerance: a correctly-rounded parser has exactly one right answer, and "close enough"
hides precisely the bugs worth finding.

```
make run
```

The corpus is built around where these functions actually break: type boundaries
(`LONG_MAX±1`, `ULONG_MAX±1`), base prefixes (`0x`, `0`, `0b`), junk and empty input, and
float rounding (subnormals, `DBL_TRUE_MIN`, round-to-even ties, hex floats, inf/nan).

## What it found (2026-09-25)

First run: **68 differences in 3161 comparisons.**

* **59 were one real defect**, now fixed: `strtol`/`strtoul` set `*endptr = nptr` on
  overflow. C17 7.22.1.4 reserves that for "no conversion performed" — overflow still has a
  valid subject sequence (the *longest* initial subsequence of the expected form), so
  `*endptr` must point **past** the digits. A caller walking a list of numbers with `endptr`
  could not advance past an out-of-range one. `strtoull.c` was already correct: it keeps
  consuming digits after overflow, which is why the defect was confined to `strtoul.c`.
* Two latent **undefined-behaviour** expressions fixed alongside it: `-LONG_MIN` and
  `-LLONG_MIN` overflow their signed types. They wrap to the right magnitude on two's
  complement, so behaviour was correct, but the compiler may assume they cannot happen —
  and both emitted `-Woverflow` on a normal host build while the target flags stayed silent.

Remaining **9 differences, both deliberate non-defects**, left as-is:

| case | count | why |
|---|---|---|
| `0b101` | 8 | glibc implements the C23 binary prefix as an extension; C17 does not require it. |
| `strtod("4.9406564584124654e-324")` | 1 | Exactly `DBL_TRUE_MIN`. libphoenix returns `0.0` where glibc returns the smallest subnormal (`0x…01`). A last-bit subnormal rounding gap; `5e-324` parses correctly. Not fixed — narrow, and worth its own change with its own test. |

Sibling harnesses: `tools/libstring-hosttest/`, `tools/libwchar-hosttest/`, `tools/libext2-hosttest/`.
