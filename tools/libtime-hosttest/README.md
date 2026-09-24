# libtime-hosttest — differential + round-trip harness for libphoenix's calendar code

Compiles libphoenix's real `time/time.c` natively and checks it against the host glibc in a
few seconds, with no Pi cycle and no cross-build.

```
make run
```

Two independent checks:

1. **Closed identity** — `timegm(gmtime_r(t)) == t` for every `t` tested. No reference
   implementation is needed for this to be authoritative.
2. **Field and string equality** against glibc's `gmtime_r` and `strftime`, over every `tm`
   field and 16 conversions, across hand-picked edges (century leap years, the 2000/2100
   rules, 32-bit boundaries, pre-Epoch), every day boundary from 1950 to 2100, and
   randomised samples spanning roughly 1600–2400.

Everything runs under `TZ=UTC` so the result is deterministic.

## What it found (2026-09-25)

**The calendar arithmetic was already correct**: 454 810 round-trips, 0 failures, and every
`tm` field matched glibc across 10.9M comparisons.

**Every difference was one bug class — signed values printed through `%u`:**

| conversion | year 1830 (or `t = -1`) printed | correct |
|---|---|---|
| `%s` | `18446744073709551615` | `-1` |
| `%y` | `4294967226` | `30` |
| `%D` | `07/18/4294967226` | `07/18/30` |
| `%Y` `%C` `%F` `%G` `%g` | same shape | — |

`%y` was doubly wrong: `tm_year` is `-70` for 1830 and C's `-70 % 100` is `-70`, not `30`, so
the arithmetic needed fixing as well as the conversion specifier.

After the fix: **10 915 440 comparisons, 0 differences.**

⚠ Two things worth keeping:

* **The target build is clean under `-Werror`.** This only surfaced because compiling
  natively let GCC's format checker see a `time_t` vs `%llu` mismatch that the target's type
  widths mask. Compiling library code with a second compiler is itself a diagnostic.
* **Fixing `%s` first revealed the other six.** They had been hidden behind the harness's
  40-line print cap — a capped diff list understates scope, so re-run after every fix rather
  than trusting the first summary.

Sibling harnesses: `tools/libfmt-hosttest/`, `tools/libnum-hosttest/`,
`tools/libstring-hosttest/`, `tools/libwchar-hosttest/`, `tools/libext2-hosttest/`.
