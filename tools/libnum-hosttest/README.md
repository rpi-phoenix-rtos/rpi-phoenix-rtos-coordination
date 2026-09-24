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

Remaining **8 differences, one deliberate non-defect**, left as-is: `0b101` for bases 0 and 2, where
glibc implements the C23 binary prefix as an extension that C17 does not require. It is classified
inside the tool so the exit status still gates.

## The strtod subnormal defect — and how I first understated it

↩ **This README originally called it "a last-bit subnormal rounding gap" and judged it too narrow to
fix. That was wrong**, and the correction is the useful part.

Probing it properly: **all 4000 of the smallest subnormals parsed as `0.0`**, and the discriminator is
**significant-digit count, not magnitude**. `strtod("4.94e-324")` was correct; `strtod("4.9407e-324")`
— the same value — returned 0. Root cause at `strtod.c`: the underflow guard rejects a number from its
exponent alone, and `exp_min = DBL_MIN_10_EXP - UINT64_MAX_DEC_DIGITS` allows for the mantissa's
digits but **not for the subnormal range**, which reaches ~17 decimal exponents below the smallest
*normal* value. A longer mantissa carries a more negative exponent for the same number, so
`49407e-328` trips a guard set at -327. The hex branches already subtract the mantissa width; the
decimal ones did not.

Why it mattered more than "subnormals are rare": the 17-digit form `%.17g` produces **always**
vanished, so any double serialised through printf and read back became 0.

Fixed by subtracting the mantissa width in the decimal branches too. Being over-generous there is
harmless — a value that really is too small still scales to zero and reports `ERANGE`.

⚠ Two methodology notes worth keeping:

* **A binary search over bit patterns gave a confident, meaningless answer** ("0.449% of subnormals
  affected"). The property is not monotonic in bit pattern; plain decimal strings disproved it
  immediately. Do not binary-search a property you have not shown to be monotonic.
* **200k random round-trips did not find this.** Random 64-bit patterns essentially never land on the
  smallest subnormals, so randomised testing and a targeted sweep catch different things — the
  round-trip arm proves the normal range is correctly rounded, the sweep proves the subnormal edge is.

Sibling harnesses: `tools/libstring-hosttest/`, `tools/libwchar-hosttest/`, `tools/libext2-hosttest/`.
