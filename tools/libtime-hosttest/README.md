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

`tdiff` runs under `TZ=UTC` so the result is deterministic.

## `tzdiff` — POSIX TZ strings (added 2026-09-28)

`localtime_r`, `mktime` (all three `tm_isdst` values), `ctime_r`, `strftime` `%Z %z %c %s`,
`tzname`/`timezone`/`daylight`, for 67 TZ strings: northern/southern DST, default US rules,
`Jn` vs `n` rules, week-5 = last, negative and >24 h change times, negative DST (Dublin),
quoted names, half/quarter-hour and seconds offsets, and the fallbacks (zone names, `:` values,
malformed DST parts).

* glibc runs with `TZDIR=/nonexistent-tzdir`, so it parses TZ as a POSIX string, as libphoenix must.
* **Transitions come from glibc alone** — a scan for `tm_isdst` flips, then bisection to the
  second — and each one is probed at −1/0/+1 s and at wall-clock times across the skipped and
  the repeated hour. Plus the closed identity `mktime(localtime_r(t)) == t`.
* For the fallback classes glibc parses differently, the reference is the fixed offset
  libphoenix documents, built from glibc's TZ-independent `gmtime_r`/`timegm`.
* **KNOWN** (counted, printed, not failures):
  * `mktime(tm_isdst=-1)` in the repeated hour: glibc's answer depends on the offset its
    *previous call* cached (priming with a summer vs a winter date flips it). libphoenix takes
    the earlier instant; the harness requires glibc's to be one of the two valid ones.
  * glibc answering with an instant that glibc's own `localtime_r` does not read back as the
    requested wall time (1 case, the permanent-DST idiom `J1/0,J365/25` at a year boundary),
    excused only when libphoenix's answer does read back correctly **by glibc**.
  * Fallback zone names: glibc keeps the leading letters (`Europe`), and names `TZ=` `Universal`.
* DST zones are compared from 1970: before that glibc computes the 1970 changes
  (`compute_change()` starts from `t = 0` for `year <= 1970`).
* `stubs.c` models the time zone mutex and aborts on recursive locking, i.e. on what would be a
  self-deadlock on the target.

```
make run   LIBPH=<libphoenix tree>                              # tdiff + tzdiff
make unity LIBPH=<libphoenix tree> TESTS=<phoenix-rtos-tests>   # the Pi's time_tz Unity group, natively
```

| tree | tzdiff | `make unity` (time_tz) |
|---|---|---|
| libphoenix `feat/posix-tz` | **12 932 381 comparisons, 0 diffs** (KNOWN: 1564 repeated-hour, 11 names, 1 glibc) | 12/12 pass |
| libphoenix master (stub) | **8 116 943 diffs** | 12/12 fail |

⚠ `scripts/run-libc-hosttests.sh` builds against `sources/libphoenix`, so `tzdiff` stays red there
until `feat/posix-tz` is merged into libphoenix master — that is the test doing its job.

It also found a pre-existing `asctime_r` defect: the day of the month was `%d`, not POSIX's
`%3d` (`Sat Mar 6` for `Sat Mar  6`).

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
