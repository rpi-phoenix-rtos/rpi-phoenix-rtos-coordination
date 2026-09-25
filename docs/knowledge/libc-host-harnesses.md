# libphoenix host harnesses — test libc changes in ~1 s, before touching the Pi

**Run this before any Pi cycle for a libphoenix change:**

```
./scripts/run-libc-hosttests.sh
```

Six harnesses under `tools/lib*-hosttest/` compile libphoenix's **real source** natively and
diff it against the host glibc. No cross-build, no hardware, a few seconds total. The bench
is exclusive and a Pi cycle costs 5–10 minutes, so the Pi should be the **confirmation** step,
not the discovery loop.

On 2026-09-24/25 this found **ten defect clusters in one night**, three of them before any
hardware was involved.

| tool | covers | found |
|---|---|---|
| `libstring-hosttest` | `string.c`, every input against a `PROT_NONE` guard page | (clean) — proves itself via `make canfail` |
| `libwchar-hosttest` | all 40 wchar functions | no defects; pinned down the P7 UTF-8 limitation |
| `libnum-hosttest` | `strtol`/`strtoul`/`strtod`… + `%.17g` round-trip | `endptr` lost on overflow; subnormals flushed to zero |
| `libfmt-hosttest` | the printf **formatter** + format→parse round-trip | ties rounded away from zero |
| `libtime-hosttest` | `gmtime_r`/`timegm`/`strftime` + `timegm(gmtime_r(t))==t` | 8 signed year/epoch conversions via `%u` |
| `libscanf-hosttest` | `sscanf` return value, values, `%n` | **OOB read of `buf[-1]`**, `%n` miscount, incomplete float items accepted |

## Why they work at all

* **`objcopy --redefine-sym`** renames only the symbols the file *defines*, so its own calls to
  `malloc`/`strtod`/`strlen` still bind to glibc. `--prefix-symbols` renames the undefined ones
  too and the link dies.
* A **shim header** supplies the one or two things the target-only headers provide —
  `__EXPORT_INLINE` for `string.c`, `-Dcount=__count` for `wchar.c`'s `mbstate_t`, `EOK`/
  `SET_ERRNO` and stubbed syscalls for `time.c`.

## Three rules these harnesses encode

**1. A clean run must be able to be dirty.** Each opens with a canary that deliberately
mis-compares and aborts if that does not register, so `diffs=0` cannot come from a comparison
path that never fires. `libstring` goes further: `make canfail` rebuilds it against a copy of
`string.c` with the **real** 2026-08 `strncmp` over-read restored and requires the guard page
to catch it — it fires on the first case, matching the recorded SuperTuxKart crash signature.

**2. Closed identities beat reference comparison.** `%.17g` uniquely identifies a double, and
`timegm(gmtime_r(t))` must equal `t`. These need no glibc to be authoritative — arithmetic is
the judge. That is what caught a plausible-looking fix to printf's scientific-form rounding
which passed every string comparison and silently broke 3 round-trips in 400 000.

**3. Compare against the RIGHT baseline, and classify what you will not change.** The wchar
harness's first run reported 138 "differences" purely because libphoenix's conversion layer is
documented C/POSIX byte-identity and glibc was running under UTF-8. Each tool now has a
`known_case()` classifier for divergences that are understood and deliberately left alone, so a
non-zero exit still means something new. ⛔ **Declassify anything you fix** — a fixed bug left
excused in a known-list hides its own regression.

## Also: compiling with a second compiler is itself a diagnostic

Four defects fell out before a single test ran, all invisible to the target build under
`-Werror` because another header or the target's type widths mask them: a missing
`<limits.h>` in `format.c` (`MB_LEN_MAX`), a missing `<stddef.h>` in `scanf.c` (`ptrdiff_t`),
the `time_t` vs `%llu` mismatch behind the whole `strftime` cluster, and `-LONG_MIN`
signed-overflow UB in `strtoul.c`.

Related: `tools/libext2-hosttest/` (the same idea for the filesystem, 21 defects),
`docs/knowledge/testing-automation.md`.
