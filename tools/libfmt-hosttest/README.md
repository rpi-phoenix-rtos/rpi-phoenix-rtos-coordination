# libfmt-hosttest — differential + round-trip harness for libphoenix's printf

`tools/libnum-hosttest/` proved `strtod` **parses** correctly. This is the other half: it
drives libphoenix's real `format_parse()` (`stdio/format.c` + `bignum.c`) natively and
checks the **formatting** side, in about a second, with no Pi cycle and no cross-build.

```
make run
```

## Two checks, and one of them needs no reference implementation

1. **Round-trip.** `"%.17g"` uniquely identifies a double, so formatting a value with
   libphoenix and parsing it back with **libphoenix's own `strtod`** must return the exact
   original bits. This is a closed self-consistency property — glibc is not the authority,
   arithmetic is — and any failure is a real defect on one side or the other.
2. **String equality** against glibc's `snprintf`, over a matrix of float and integer
   conversions with assorted widths, precisions and flags (663 comparisons).

A canary compares `"a"` against `"b"` first and aborts if that does not register, so a clean
run cannot come from a comparison path that never fires.

## What it found (2026-09-25)

**Round-trip was already sound**: 400 000 random doubles, 0 mismatches.

**Ties rounded the wrong way.** libphoenix rounded half **away from zero**, where IEEE-754
default rounding — and glibc, and musl — round half **to even**:

| case | was | glibc |
|---|---|---|
| `%.0f` of 0.5 | `1` | `0` |
| `%.0f` of 2.5 | `3` | `2` |
| `%.2f` of 0.125 | `0.13` | `0.12` |

Fixed in `format_sprintfDecimalForm()`: 7 differences → 1. Only *exact* ties are affected;
anything strictly past halfway still rounds up.

## ⛔ The scientific form is deliberately NOT fixed — read this before trying

`%e` still rounds ties away from zero (`"%.0e"` of 2.5 gives `3e+00`, glibc gives `2e+00`).
Copying the decimal form's ties-to-even test into `format_sprintfScientificForm()` **looks**
right and silently breaks round-tripping: `bd->num` is *not* the remainder beyond the
rounding digit on that path, so exact ties get declared where there are none, values round
down, and `%g` then strips the resulting trailing zeros. Observed when it was tried:

* `printf("%.17g", 1.0000000000000001e+300)` collapsed to `"1e+300"`
* 3 in 400 000 random doubles stopped surviving a format/parse round-trip

A lost round-trip silently corrupts any serialised double; a last-digit tie is cosmetic. The
change was reverted, the reasoning left in a comment in `format.c`, and the divergence is
classified as `FKNOWN` here so the exit status still gates on anything new. Fixing it
properly means finding where that path keeps its residue — this harness reproduces both
symptoms in about a second, so the work is cheap to attempt and cheap to disprove.

## Mechanics

`format.c` needs exactly one Phoenix header (`sys/minmax.h`), copied into `shim/` by the
Makefile. `strtod` is compiled separately and its symbols renamed with
`objcopy --redefine-sym` so it does not collide with glibc's while its own internal calls
still bind normally.

Sibling harnesses: `tools/libnum-hosttest/`, `tools/libstring-hosttest/`,
`tools/libwchar-hosttest/`, `tools/libext2-hosttest/`.
