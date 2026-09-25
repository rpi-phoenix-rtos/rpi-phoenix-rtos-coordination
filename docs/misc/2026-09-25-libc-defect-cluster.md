# 2026-09-25 — the libc defect cluster, and the issue-list audit around it

Detail for the night of 2026-09-24/25, moved out of `docs/inprogress/WEEK-2026-W39.md`,
which is deliberately kept short. The weekly log's **§0** carries the one-screen summary
and points here.

Sections run in the order they happened: **0z** is the first find, **08** the last.

---

## 0z. ★★★★ 2026-09-24 — P6 ROOT-CAUSED: `setvbuf()` threw the buffer away on an equal-size request

P6 (`echo > file` ~50× slower than `dd`) is **not** about `echo`, bash, ext2 or write size. In
libphoenix `stdio/file.c`, `setvbuf()` cleared `stream->buffer` and only re-allocated it under
`else if (old_siz != size)` — **with no matching `else`**, so an equal-size request left the stream
with `bufsz = 4096` and `buffer == NULL`, which `fwrite_unlocked()` reads as "unbuffered".

That is not an edge case: `fopen` always allocates `BUFSIZ`, and `setlinebuf(s)` is
`setvbuf(s, NULL, _IOLBF, 0)` whose 0 becomes `BUFSIZ` — so the sizes **always** matched and the
standard idiom silently *unbuffered* every stream it was called on. bash runs it on stdout at
startup. Combined with `vprintf` feeding one char at a time (`printf_feed` → `putchar`, unlike
`vfprintf`'s 256-byte staging), that is **one `write()` syscall per byte** — 12.5 ms each on ext2,
so 513 bytes = 6.4 s. Measured 6.4 s/file: exact.

**The measurement that cracked it** — same bytes, same directory, different writer:
builtin `echo` 32 s / 5 files · builtin `printf` 32 s · **external `/usr/bin/echo` 1 s** · `dd` 0 s.
Same target ⇒ not the filesystem; both builtins ⇒ their shared `stdout`, not the builtin.
⛔ Every earlier attempt to exonerate libphoenix used a `fopen`'d file, which never calls `setvbuf` —
I kept re-testing the path that works. The existing `stdio_bufs` cases all pass a *caller* buffer,
so the NULL-buffer branch real ports use had **no coverage at all**.

✅ **FIXED AND GATED** — libphoenix `008e25d`, tests `6e7a7f6`. `setvbuf()` now reuses the existing
buffer when the size is unchanged and it is ours; a caller-owned buffer is never reused. Two adjacent
ownership bugs fell out of the same restructure: an equal-size `_IONBF` request (`setbuf(s, NULL)`)
leaked the old buffer, and a replaced buffer inherited stale `bufpos`/`bufeof`.

**Gate, in order:** 3 new `stdio_bufs` cases built **without** the fix first and shown red
(`9 Tests 3 Failures`) → fix applied → green (`0 Failures`) → full stdio (94) and printf (133) suites
clean → **bash relinked and hash-verified** `dd404338`→`36514daa` → benchmark re-run.

| writer, 5 files x 513 B on ext2 | before | after |
|---|---|---|
| bash builtin `echo` | 32 s | **0 s** |
| bash builtin `printf` | 32 s | 1 s |
| external `/usr/bin/echo` | 1 s | 0 s |
| `dd` | 0 s | 1 s |

Output size still 513 B, so the work happened. ⚠ The relink step was load-bearing: the `--scope core
--with-ports` build left bash's hash **unchanged**, because it is `--enable-static-link` — running the
benchmark then would have read 32 s and looked like a failed fix. `build-port.sh bash` forced it.
P6 removed from KNOWN-ISSUES, archived in `docs/done/closed-issues-archive.md`.
➡ Wider than bash: any port calling `setlinebuf()`/`setvbuf(…, NULL, …)` has run unbuffered — psh's
own `top` included. Secondary P6 lever still open and unfiled: ext2 appending write 12.5 ms vs
in-place overwrite 169 µs (defer `ext2_sb_sync()` to unmount/sync, ~1.5×).

---

## 0y. ★★ 2026-09-24 — second stdio defect, found by the same question: `freopen()` never cleared the indicators

Audit follow-on from 0z, asking of each buffer-management path *which **branch** do the existing tests
take* rather than *do they pass*. One real defect (libphoenix `c5c6036`, tests `7d7e229`, pushed):

**`freopen()` never touched `flags`**, though C17 7.21.5.4 requires the error and end-of-file
indicators to be cleared. `F_EOF` is tested at the **top** of `fgetc_unlocked()`, so a stream that had
already hit EOF kept returning EOF after being pointed at a different file — **the reopened file read
as completely empty, its data present but unreachable, with no error reported anywhere.** The fix also
clears `F_WRITING` and parks `bufpos`/`bufeof` at the fresh-`fopen` state. Gate: red first
(`10 Tests 1 Failures`, at the `feof` assertion) → green (`0 Failures`), full stdio suite 95/0.

The pre-existing `freopen` test passed throughout — it only asserted the returned pointer was
non-NULL. Same shape as 0z, where all six `setvbuf` cases passed a *caller* buffer.

**Checked and CLEARED in the same pass — do not re-walk:** `rewind()` (correctly clears `F_ERROR`,
`fseek` clears `F_EOF`), `fflush(NULL)` (does route to an all-streams flush), and
`stream->mode & O_RDONLY` in the write path — which reads like the classic POSIX bug since `O_RDONLY`
is normally `0`, but Phoenix defines it as `0x1`, so the check works.

---

## 0x. 2026-09-24 — wchar audited on the HOST: no defects, one limitation written down

Applied the libext2-hosttest pattern to libphoenix's `wchar`: `tools/libwchar-hosttest/` compiles the
real `wchar/wchar.c` **natively**, renames its 40 exported symbols with `objcopy --redefine-sym` (so
its own calls to `malloc`/`strtol` still bind to glibc), and diffs every function against glibc.
4083 comparisons in ~1 s, no Pi cycle. A canary compares 1 vs 2 first, so `diffs=0` cannot come from
a comparison path that never fires.

✅ **All 17 locale-independent functions match glibc EXACTLY** (`wcslen wcscmp wcsncmp wcschr wcsrchr
wcsstr wcsspn wcscspn wcspbrk wmemchr wmemcmp wmemset wcscpy wcsncpy wcscat wcstol` + end pointer).
That is a positive result: those paths are correct, not just untested. **No defects found.**

⛔ **My first run was measured against the wrong baseline** and reported 138 "differences". libphoenix's
conversion layer is **C/POSIX byte-identity**, stated in a comment above every function; comparing it
to glibc under UTF-8 finds design, not bugs. Under `C` the residue is 122, and all of it is either
that same charmap difference (glibc's C locale is ASCII-only and rejects bytes ≥ 0x80) or `wcwidth`.

➡ Filed as **P7** (new row, not a regression — long-standing deliberate behaviour that was simply
never written down): no UTF-8 means `wcstombs` returns **-1** for anything above U+00FF, `mbstowcs` of
UTF-8 input yields one wide char **per byte**, and `wcwidth` reports 1 column for CJK/emoji. Relevant
to `%ls`/`%lc`, implemented via `wcrtomb` earlier today. Exposure per port is **not** audited; a port
with its own codecs (CPython) is unaffected.

## 0w. ★★ 2026-09-25 — a GUARD-PAGE harness for string.c, checked against a real past defect

`tools/libstring-hosttest/` compiles libphoenix's real `string/string.c` natively and places every
input so its last byte is the last byte of a mapped page, with the next page `PROT_NONE`. Any bounded
function reading one byte too far takes SIGSEGV, caught and attributed to the function and case.
Results are also diffed against glibc. 1368 checks, ~1 s, no Pi cycle.

✅ **`STRING-HOST total=1368 overreads=0 wrong=0`** — clean.

★ **The part that makes the clean result mean something:** `make canfail` rebuilds the harness against
a copy of `string.c` with the **2026-08 `strncmp` defect put back** (bound tested after the
dereference) and requires it to fire. It catches it on the **first** case — `w0/w0/n1`, a 1-byte
unterminated string ending at a page boundary with `n=1`, which is exactly the recorded STK crash
signature (`far=0x0cf0e000`, `n=1`, `s1` = last byte of a mapped page, from AngelScript's tokenizer).
That defect originally cost a debugging session and read as an intermittent game crash; this finds it
on the host in under a second. A run also opens with a canary that reads past the guard, so
`overreads=0` cannot come from protection that was never armed.

Together with `tools/libwchar-hosttest/` (0h. above), libphoenix's two biggest pure-computation
surfaces now have host harnesses that need no hardware.

## 0v. ★★ 2026-09-25 — `strtol`/`strtoul` lost the end pointer on overflow (found on the host)

`tools/libnum-hosttest/` compiles libphoenix's `strtoul.c`/`strtoull.c`/`strtod.c` natively and diffs
value, end pointer and the ERANGE decision against glibc over ~3200 cases in ~1 s. `strtod` is compared
**bit-exactly** on its IEEE-754 payload — a tolerance would hide the rounding bugs worth finding.

First run: **68 differences. 59 were one defect.** `strtol`/`strtoul` stored `nptr` in `*endptr` on
overflow. C17 7.22.1.4 reserves that for *no conversion performed*; an out-of-range number still has a
valid subject sequence (the **longest** initial subsequence of the expected form), so the value
saturates, `errno` is `ERANGE`, and `*endptr` must point **past the digits**. ➡ Practical bite: a
caller walking a list of numbers with `endptr` cannot advance past an out-of-range one and stalls on
it. `strtoull.c` was already correct (it keeps consuming after overflow), so this was confined to
`strtoul.c`. Fixed: libphoenix `3d57bcd`, test `4c75a8d` — **0 real differences after**.

Two latent **UB** expressions fixed alongside: `-LONG_MIN` / `-LLONG_MIN` overflow their signed types.
They wrap to the right magnitude on two's complement so behaviour was correct, but the compiler may
assume signed overflow cannot happen — and both warned on a plain host build while
`syntax-check.sh` under the **real target flags stayed silent**, which is why they had survived.

⚠ **Same pattern as 0y/0z for the third time:** the existing `strtol_min_max` cases all pass `NULL`
for `endptr`, so the ERANGE path had no coverage. Ask which *branch* the tests take.

Two remaining differences are deliberate and classified as known in the tool (so its exit status still
gates): glibc's C23 `0b` prefix extension, and `strtod("4.9406564584124654e-324")` — exactly
`DBL_TRUE_MIN` — returning `0.0` instead of the smallest subnormal. The latter is a genuine last-bit
gap, left for its own change with its own test; `5e-324` parses correctly.

✅ **Gated on hardware and pushed.** Pre-fix build: `46 Tests 1 Failures`, failing exactly at the
`endptr` assertion with the other 45 `strto` cases passing. Post-fix: group `46 Tests 0 Failures`,
full stdlib suite `94 Tests 0 Failures`. libphoenix `3d57bcd`, tests `4c75a8d`, both on master.

## 0u. ✅ 2026-09-25 — SIX-APP SHOWCASE GATE 6/6 on the two stdio fixes

The `setvbuf` (0z) and `freopen` (0y) fixes touch every `FILE*` in the system, and 682 libc unit tests
do not prove the desktop and the games still work. Ran the full gate (`--label stdio-fixes`, ~35 min):

| app | rc | psh prompt | faults | frames |
|---|---|---|---|---|
| x (wmaker + GL + xbill + xclock) | 0 | yes | **0** | — (23 snaps) |
| qspasm | 0 | yes | **0** | 9414 |
| q3 | 0 | yes | **0** | 9660 |
| q2 | 0 | yes | **0** | 10589 |
| vkq | 0 | yes | **0** | 5072 |
| stk | 0 | yes | **0** | 1734 |

The script's own warning is that this is **mechanical only** — a clean log does not mean anything was
drawn — so the frames were also looked at: the X desktop composites wmaker + a GL window + xbill +
xclock correctly; STK renders a fully-lit 3D race with HUD, minimap and speedometer; Q2 renders full
textured 3D at 38.78 fps (*not* black, which is its historical failure). #67 vkQuake torch ROI scoring
also passed, 15/15 frames at the reference viewpoint against a required 2.

➡ Conclusion: making every stream actually buffered changed no rendering behaviour anywhere.

## 0t. ★★★ 2026-09-25 — `strtod` flushed representable subnormals to zero (and I understated it first)

Found by extending `tools/libnum-hosttest/` with a **randomised `%.17g` round-trip** arm (a correctly
rounded parser must return the exact original bits, so any mismatch is a defect, not a disagreement).
300k round-trips are clean — but they do **not** reach the smallest subnormals, so a targeted sweep
was needed too.

↩ **I first recorded this as "a last-bit subnormal gap" and judged it too narrow to fix. Wrong.**
**All 4000 of the smallest subnormals parsed as `0.0`**, and the discriminator is **significant-digit
count, not magnitude**: `strtod("4.94e-324")` correct, `strtod("4.9407e-324")` — same value — returned
0. Root cause: the underflow guard rejects a number from its exponent alone, and
`exp_min = DBL_MIN_10_EXP - UINT64_MAX_DEC_DIGITS` allows for the mantissa's digits but **not for the
subnormal range**, which reaches ~17 decimal exponents below the smallest *normal*. A longer mantissa
carries a more negative exponent, so `49407e-328` trips a guard set at -327. The **hex** branches
already subtract the mantissa width; the decimal ones did not.

➡ **Why it mattered:** the 17-digit form `%.17g` produces *always* vanished — any double serialised
through printf and read back became 0.

Fixed libphoenix `e002eb6`, test `5edd928`. Gate: pre-fix `47 Tests 1 Failures`; post-fix 47/0 and the
full stdlib suite 95/0. Host sweep 4000 mismatches → **0**, 300k round-trips still exact.

⚠ **Two methodology notes.** A binary search over bit patterns produced a confident and *meaningless*
number ("0.449% of subnormals"); the property is not monotonic in bit pattern, and plain decimal
strings disproved it at once — do not binary-search a property you have not shown to be monotonic.
And the now-fixed case was **removed from the harness's known-non-defect list**, since leaving a fixed
bug excused there would hide a regression.

## 0s. ★★ 2026-09-25 — printf rounded ties the wrong way; and one fix I tried, broke, and reverted

`tools/libfmt-hosttest/` drives libphoenix's real `format_parse()` natively — the FORMATTING half of
numeric I/O, after `libnum-hosttest` settled parsing. 663 conversions vs glibc **plus** a randomised
`%.17g` format→parse round-trip through libphoenix's *own* `strtod`. That round-trip needs no
reference implementation to be authoritative: `%.17g` uniquely identifies a double, so arithmetic is
the judge, not glibc.

✅ **Round-trip was already sound**: 400k random doubles, 0 mismatches.

**Defect: ties rounded away from zero** where IEEE-754 default rounding (and glibc, and musl) round
**to even** — `%.0f` of 0.5 gave `1` not `0`; `%.2f` of 0.125 gave `0.13` not `0.12`. Fixed in the
decimal form (libphoenix `a12f7ac`, test `018c87a`): 7 diffs → 1. Gate: pre-fix `31 Tests 1 Failures`;
post-fix 31/0, full printf **134/0**, full stdio **95/0**.

⛔ **The scientific form is deliberately NOT fixed, and this is the useful part.** I copied the same
ties-to-even test into `format_sprintfScientificForm()`. It looked right and **silently broke
round-tripping**: `bd->num` is not the remainder beyond the rounding digit on that path, so exact ties
were declared where there were none, values rounded down, and `%g` stripped the resulting zeros —
`%.17g` of `1.0000000000000001e+300` collapsed to `"1e+300"`, and **3 in 400k** doubles stopped
surviving a round-trip. Reverted; what was tried and what it broke is now a comment in `format.c`, the
test explicitly does not assert that case, and the harness classifies it `FKNOWN` so it still gates.
A last-digit tie is cosmetic; a lost round-trip silently corrupts every serialised double.

Also fixed: `format.c` used `MB_LEN_MAX` without `<limits.h>` (in the `%ls` code added earlier today).
It built only because another header pulled it in on this target — compiling natively exposed it.

## 0r. ★★★ 2026-09-25 — `strftime` printed signed years and epochs through `%u` (8 conversions)

`tools/libtime-hosttest/` compiles libphoenix's real `time/time.c` natively: `gmtime_r` fields and 16
`strftime` conversions vs glibc, plus the **closed identity** `timegm(gmtime_r(t)) == t`, across
hand-picked edges, every day boundary 1950→2100, and randomised samples spanning ~1600–2400.

✅ **The calendar arithmetic was already correct** — 454 810 round-trips, **0 failures**, every `tm`
field matching glibc over 10.9M comparisons.

**Defect: eight conversions passed a signed value to `%u`.** Anything before the Epoch or before 1900
printed as a huge number: `%s` of `t=-1` → `"18446744073709551615"` instead of `"-1"`; `%y` of 1830 →
`"4294967226"` instead of `"30"`; `%D` → `"07/18/4294967226"`. Also `%Y %C %F %G %g`.
★ `%y`/`%g` were **doubly** wrong — `tm_year` is `-70` for 1830 and C's `-70 % 100` is `-70`, so the
year-within-century needed normalising to 0..99, not just a different conversion specifier.

Fixed libphoenix `9e69afa`, test `87413da`. Gate: pre-fix `4 Tests 1 Failures`; post-fix 4/0 and the
full time suite **44/0**. Host: 10 915 440 comparisons → **0 diffs**.

⚠ **Two method notes.** (1) The **target build is clean under `-Werror`** — this surfaced only because
compiling natively let GCC's format checker see a `time_t` vs `%llu` mismatch that the target's type
widths mask. *Compiling library code with a second compiler is itself a diagnostic.* (2) **Fixing `%s`
first revealed the other six**, which had been hidden behind the harness's 40-line print cap — the
same way the `strtod` subnormal scope was understated earlier. Re-run after every fix; never trust the
first capped summary.

## 0q. ★★★ 2026-09-25 — scanf read one byte BEFORE its buffer on every suppressed conversion

`tools/libscanf-hosttest/` compiles libphoenix's real `stdio/scanf.c` natively: 1564 comparisons vs
glibc over return value, parsed values and `%n`.

**Defect cluster (libphoenix `cbd90a9`, test `a56d255` — ✅ gated and pushed: pre-fix
`10 Tests 1 Failures`; post-fix 10/0, scanf-advanced **34/0**, scanf-basic **48/0**).**
`scanf.c` recorded consumption as `p - buf`, the count of characters **copied for conversion**. A
suppressed (`%*`) conversion copies nothing — deliberately, that is what lets its width run unbounded
— so `p - buf` was 0 and three things broke:

* **`%n` short by the suppressed digits**: `sscanf("12 34", "%*d%d%n", &v, &n)` set `n=3`, glibc 5.
* **`%*i`/`%*x` could not see a `0x` prefix** — the test is `p == buf + 1`, unreachable when nothing
  is copied.
* ⚠ **`c = ((unsigned char *)p)[-1]` read ONE BYTE BEFORE `buf`** whenever nothing had been copied,
  which under `SUPPRESS` is *always*. If that stack garbage was `'x'`/`'X'` it then did
  `--p; inp--; (*inr)++`, un-reading a character it never read. A memory-safety bug, not just a
  wrong number.

Plus a 4th missing include found the same way (`<stddef.h>` for `ptrdiff_t`), invisible on target.

**87 divergences measured, understood, and deliberately NOT changed** (classified in the tool so it
still gates): the EOF-vs-0 return semantics (C17's wording on whether a *suppressed* conversion
"completes" is ambiguous, and our reading of `%*d` is arguably the more literal one — changing scanf's
return values on a contested reading risks every port); the C23 `0b` prefix; and the float
incomplete-item defect, which has since been **FIXED** (see 0o below).

## 0p. ✅ 2026-09-25 — SECOND showcase gate 6/6 (printf rounding + strftime changes)

`format.c` backs every `printf` in the system and `time.c` every date, so both were re-gated after
0s/0r. `--label libc-round2`, 6/6, rc=0, psh prompt, **0 faults each** (frames: qspasm 9462, q3 9874,
q2 10360, vkq 8017, stk 1761). #67 torch ROI: **PRESENT**. Frames looked at, not just logged — vkQuake
renders the start map with both archway torches lit, HUD 100/25 at 32 fps.

## 0o. ★★ 2026-09-25 — scanf accepted an INCOMPLETE float item (the deferred fix, now done)

Deferred last turn on purpose: it needed a restructure rather than a patch, and the bench was busy.
Picked up with the Pi free and the harness in place.

`scanf`'s float path called `strtod` and took its endptr. `strtod` stops at the longest **valid
prefix** — right for `strtod`, wrong for `scanf`: C17 7.21.6.2p12-13 defines the item as the longest
sequence that *is, **or is a prefix of***, a match, and **fails** if that item is not itself a match.
So `sscanf("1e", "%lf", &d)` returned 1 with `d = 1.0`, accepting input glibc refuses — as did
`"1e+"`, `"1.5e"`, `"1east"`, `"0x"`, `"0xg"`. Any port validating with `sscanf(...) == 1` accepted
malformed numbers. ⭑ **Not a `strtod` bug**: `strtod("1e")` is correctly 1 with endptr 1; the two
functions have different contracts.

Fix detects the two shapes where the item provably continues but cannot complete (exponent marker
with no digits; `0x` with no hex digits). ★ **The two guards that keep it honest, both asserted:**
`hasExp` — a second `e` does not extend an item that already has one, so `1e5e` is still 100000 — and
`e` being a hex digit, so `0x1e` (30) and `0x1ep2` (120) are consumed whole and never reach the check.

libphoenix `ad351a3`, test `d0b75a9`. Gate: pre-fix `13 Tests 1 Failures`; post-fix 13/0,
scanf-advanced **35/0**, scanf-basic **48/0**. Harness: 87 classified divergences → **85, diffs=0**,
and the fixed cases were **declassified** — a fixed bug left excused in a known-list hides its own
regression.

## 0n. ✅ 2026-09-25 — THIRD showcase gate 6/6 (scanf changes) + the harnesses made runnable

`--label scanf-round3`: 6/6, rc=0, prompt, **0 faults each** (qspasm 9465, q3 9173, q2 10373, vkq
7951, stk 1812 frames). Torch ROI **PRESENT**. STK — the heaviest config parser, so the best check for
a `scanf` regression — renders a full race with HUD, lap 2/2 and minimap.

**`./scripts/run-libc-hosttests.sh`** now runs all six harnesses plus libstring's `canfail`
self-check, and `docs/knowledge/libc-host-harnesses.md` + an AGENTS.md pointer make them
discoverable. ⚠ Building the aggregate exposed two gaps in my own `libwchar-hosttest`: **no `run`
target** (so `make run` returned *make's* exit code, not the harness's — the same shape as grading by
`rc` instead of tagged output) and **no divergence classifier**, which left it permanently red. Cases
are now marked by whether their INPUT is non-ASCII; classifying by *function* would have masked a real
defect in those same functions. One ASCII case survives and is documented, not changed:
`mbtowc("", 0)` → -1 here vs glibc's 0 (with `n == 0` no byte may be examined, so "cannot form a
character" is at least as defensible; C17 7.22.7.2 does not settle it).

✔ **Defect-class audit closed empirically:** the remaining `%u` conversions in `time.c` take
`tm_mday`/`tm_hour`/`tm_wday`… which are non-negative for any valid `struct tm` — and the harness
sweeps those same fields across 10.9M comparisons with 0 diffs, so this is measured, not inspected.

## 0m. 2026-09-25 — P3's EL2 question ANSWERED (hypothesis was wrong), and D8's WHD claim MEASURED

**P3 / TD-20 (`dc zva` disabled in `hal_memset`)** was blocked on "prove the EL2 DC-ZVA trap state,
HW-only, does not repro in QEMU". That proof costs one boot: `DCZID_EL0` is **EL0-readable**.
`tools/dczid-probe/` on the real Pi 4: `raw=0x4`, **DZP=0 (PERMITTED)**, `BS=4` (64-byte block), and
it *executes* `dc zva` — 64 bytes zeroed, neighbouring byte intact (that check matters: `dc zva`
zeroes the block **containing** the address, so a misaligned pointer clobbers neighbours).
**`HCR_EL2.TDZ` is not set.** ⛔ Do not re-run that probe.

⚠ **I did NOT lift the gate, and the reason is the interesting part.** The measurement kills the trap
hypothesis but does not cover the failing context: `hal_memset`'s own header says it *"may not work for
uncached memory"*, and the recorded failure is a **hang with no exception output** in `_log_init`'s
first large zeroing right after the D-cache is enabled. **A trap would have raised an exception; a
hang fits `dc zva` against memory that is not Normal cacheable at that moment.** Lifting it now needs
a whole-kernel argument — *no caller of `hal_memset` can pass uncached memory* — not another
measurement. A bounded boot test was considered and rejected: even a clean boot would not prove the
uncached case, so it would risk the bench for no decisive information.

**D8 (unmerged branches):** the four `lwip/*` branches each hold real unique content, so none is
redundant (the quoted "+115/+178/+6/+146" are **commits, not lines**). But the thing two of them
remove is now verified dead weight: `lwip/wi-fi/whd/` is **1.6 MB / 75 files** of vendored Cypress
WHD whose build is gated on `LWIP_WIFI_BUILD`, which **every** project sets to `no`; nothing outside
`wi-fi/` references `whd_`; and our own `rpi4-wifi.c` has **0** `whd_` references. "Unused" is now
measured, not assumed. Third-party Apache-2.0 code in a to-be-published repo ⇒ **owner's call**, left
alone.

## 0l. 2026-09-25 — G3: I built a checker, then deleted it because the codebase already had better

Chasing G3's "Phoenix has no ELF build-id, so the cache keys on shader source only and a host
toolchain change invalidates nothing", I wrote `check-v3d-cache-version.sh` to flag driver commits
since the last `V3D_PHX_CACHE_VERSION` bump. It reported **65 commits since the bump** — which looked
like a real finding.

⛔ **It was a false alarm, and the tool was wrong to exist.** `sync-netboot-tree.sh:99-115` already
invalidates the cache by **sha256 of the three compiled Mesa archives** (`libv3d-phoenix.a`,
`libGL-phoenix.a`, `libv3dv-phoenix.a`). That is **content-based**, so it catches precisely the case
the row calls unprotected — a toolchain/sysroot change that alters the compiled driver without
touching shader source — and it needs nobody to remember a version bump. A git-history checker is
strictly weaker and adds noise. **Deleted rather than shipped**, and G3 now records the real
mechanism so nobody re-solves it.

✔ Incidental confirmation: of those 65 commits, **none** touched a Mesa compiler source — all are
winsys / power / GPU-server glue (`v3d_phoenix_winsys.c` 49, `v3d_phoenix_power.c` 10, `v3d_gpu.c` 6).
QPU codegen lives in the upstream Mesa tree the build scripts compile, not in this glue.

## 0k. ✅ 2026-09-25 — D6 closed: `vkCmdSetDepthBias` is real again

The no-op's comment blamed V3DV's unpopulated `CmdSetDepthBias2EXT` dispatch slot and proposed
populating it. ⭑ **That was unnecessary:** `vk_common_CmdSetDepthBias2EXT` is a **defined symbol** in
`libv3dv-phoenix.a` and the driver is linked in-process, so the trampoline calls it **directly** and
never touches the empty slot — the fix stays in the port's glue instead of V3DV's generated code.
ports `vk_trampolines.c`; gated with the gate's own `--only vkq` arm: rc=0, **0 faults**, 7975 frames,
torch ROI PRESENT, frame unchanged. ⚠ **No visible difference demonstrated** — the justification is
removing a known-broken path, not a proven visual win.

⚠ **Two assertion steps earned their keep.** (1) `build-port.sh` left the export at **last night's**
binary — testing then would have "confirmed" a fix that was not installed; the sync moved it and the
size changed 13116912 → 13117520. (2) **`strings` was not proof**: it finds `CmdSetDepthBias2EXT` in
the binary either way, because Mesa's dispatch tables contain that name — a false green for exactly
the reason the project's "never grade presence by grepping for a word" rule exists. The real evidence
was `vk_trampolines.o`: `vkCmdSetDepthBias` defined (T) with an undefined reference (U) to
`vk_common_CmdSetDepthBias2EXT`, which then linked.

## 0j. ✅ 2026-09-25 — FULL libc suite sweep: all 21 suites, 0 failures

The night changed libphoenix in ten places across seven files, but only **five** suites had been run.
Swept the remaining sixteen in three cycles — **773 tests, 0 failures, 0 faults**:

| batch | suites | tests |
|---|---|---|
| A | string 208 · math 90 · misc 212 · signal 32 · semaphore 3 · pthread-tsd 4 | **549** |
| B | dirent 38 · exit 31 · poll 2 · statvfs 22 · pthread 30 · libcache 40 | **163** |
| C | unix-socket 38 · posixsrv 16 · inet-socket 7 | **61** |

★ **Why these, specifically.** Two of the night's changes carry cross-suite risk that their own tests
cannot see: `setvbuf` altered buffer **ownership** (when the old buffer is freed vs reused), and
`scanf` added state tracking to a hot parsing loop. Both are the shape that passes targeted tests and
then surfaces as a double-free or stale pointer somewhere unrelated. **`exit` (stream teardown) and
`pthread` (concurrent stdio locks) are the suites that would catch it** — both clean. `libcache` was
included as a control: unrelated to any change, so a failure there would have meant the environment,
not the work.

Combined verification for the night: **all 21 libc suites green**, three 6/6 showcase gates with 0
faults, and six host harnesses clean via `./scripts/run-libc-hosttests.sh`.

## 0i. 2026-09-25 — D7 checked against source: one listed deviation does not exist, and the framing is off-policy

**(1) `TD-14-tiocspgrp-pgrp` has no marker in the tree.** The TD-14 IDs actually in source are
`TD-14` (×6), `-console-alias`, `-console-open-fastpath`, `-devfs-direct` (×2), `-psh-retry`, `-tty`,
`-ttyopen-nonfatal`. The TIOCSPGRP code is there (`libtty.c:576-580`) but carries **no marker and no
deviation comment** — it reads as ordinary code, with the same `FIXME: check permissions` style
upstream uses on the adjacent `TIOCSCTTY`. So D7 lists an ID that does not exist — the **same
staleness class D2's row already had to correct**. The other two markers are live.

**(2) The row's framing conflicts with standing owner policy.** Verified quote (2026-09-08): *"We
don't send anything upstream. We get changes from upstream to our fork and we maintain our fork."*
Under fork-only, a deviation that works as designed is the **normal state**, not debt, and
"upstreamable as-is" is not a pending decision. ➡ Suggested: close D7 or re-scope it to the two live
markers as documented behaviour. **Left to the owner** — it is their issue list, and the call is about
policy rather than code.

⭑ Checked-and-rejected this turn, with reasons: **P5** (boot-order race) — its own row says invisible
in practice and the fix edits plo boot config, which has bricked boots here before; **C5** (audio DMA
stall) — timing-sensitive heisenbug whose rate moves with any relink, so 25/25 clean runs bound it
rather than clear it; **D4** (plo vector table) — real, but payoff only appears when something *else*
crashes, against boot-breaking risk; **G2** — needs a physical mouse drag.

## 0h. 2026-09-25 — G1's headline numbers are stale: it is 14 fps, not 10

Re-measured from tonight's gates at the **same 640×480** the row specifies (verified from
`gl-x11: start (640x480, …)` before comparing — a different window size would have made the
comparison meaningless). Two independent gates hours apart agree: **70.6 ms/frame, `put` 33.9 ms,
14.2 fps**, against the row's **96 ms / 58 ms / ~10 fps**. Frame time **1.36×** better, `put`
**1.71×** better.

⭑ **1.71× is exactly the ceiling the row attributes to its "half-proven" point fix**, which suggests
that fix landed and the row was never updated. ⚠ **Causation not verified** — only the coincidence is
reported, not a claim.

Current breakdown: `put` 33.8 (48 %) · `read` 12.5 (18 %) · `pack` 6.4 (9 %) · `draw` 1.6 (2 %) ·
**~16 ms (23 %) unaccounted** by the app's own counters. That unaccounted slice is the obvious next
measurement if anyone revisits G1. ➡ Plan against 14 fps.

## 0g. ★★ 2026-09-25 — C1: 0 guard fires in THREE gates, detector proven armed

Goal #2 of the cron. Measured from tonight's three showcase gates (18 app runs, 3 STK):
**0 `malloc:   why   = ` guard fires**, against this row's baseline of **5× on the 2026-09-22 gate**.

⭑ **Proving the detector was armed is the whole value** — a silent detector and a clean run are
indistinguishable. Each gate emitted **93/96/97** `malloc: C1-hunt: created a 0xd000 heap` traces, and
both that trace and `malloc_chunkValidWhy()` are **unconditional** in `malloc_dl.c` (runtime size test
+ 16-report cap, no `#ifdef`). So these are **stock builds**, not the instrumented ones that C1's
layout-sensitivity makes read 0 — `-DV3D_C1_HUNT` gates the *V3D driver* instruments, not this code.

⚠ **My first count was a false negative I created.** I grepped `why=` → 0 fires. The guard actually
prints `malloc:   why   = ` through `malloc_debugHex`, so the pattern never could have matched. That
is precisely the "never grade presence by grepping a log for a word" rule, and it would have produced
a confident wrong answer about the cron's #2 goal. Re-counted with the real string.

➡ **Not proof of absence** — 3 gates cannot clear a defect that moves with a relink.
⭑ **Correlation, explicitly not causation:** the same night fixed two real allocation-lifecycle bugs —
`setvbuf()` **dropping** a stream's buffer on an equal-size request, and **leaking** the old one on an
equal-size `_IONBF` (`setbuf(s, NULL)`). Both change malloc traffic and object lifetimes. Untested
link; the cheap next datapoint is more gates.

## 0f. ★★ 2026-09-25 — C1 STK bench: 0 fires in 5 solid trials (8 solid STK runs total tonight)

Picked STK over more gates deliberately: a gate is ~40 min for **one** STK run, and STK is the vehicle
whose `0xd000` bin-14 heaps match every archived C1 fire — so back-to-back STK trials buy ~6× the
exposure per hour.

| trial | fires | armed (`0xd000` traces) | frames | faults |
|---|---|---|---|---|
| T1–T5 | **0** | 13 / 13 / 15 / 13 / 16 | ~620 each | 0 |
| T6 | 0 | 14 | **10** | 0 |

⚠ **T6 excluded from the count**: still compiling shaders when the 150 s window closed (0.24–0.75 fps,
`kartDirt shader is missing`), so ~0 gameplay exposure. Counting it would inflate the bound.

➡ **Running total: 8 solid STK runs, 0 fires** (5 bench + 3 gate, ~8 400 frames). **A bound, not a
clearance** — C1 moves with a relink.

⚠ **Two measurement errors of mine, both recorded because each would have been a confident wrong
answer on the cron's #2 goal:** (1) I graded fires with `why=`, but the guard prints
`malloc:   why   = ` via `malloc_debugHex` → a false 0 I manufactured. (2) I verified "STK actually
ran" with `Fastest lap`/`FPS:` — text STK renders to the **screen**, never to the UART — which
reported `stk-ran=0` for all six trials and would have invalidated the whole bench. The real
run-evidence is `v3d-winsys: flipstat … (total N)`. **Assert the work happened, and check the string
the code actually prints.**

## 0e. ★★★ 2026-09-25 — C1 bound now 16 solid STK runs at 0 fires

Second bench, 300 s capture: **8/8 solid trials, 0 fires**, 616–629 frames each, detector armed
(12–16 `0xd000` traces), 0 faults. The wider window fixed the startup-only trial (8/8 vs 5/6 at 150 s).

➡ **Running total: 16 solid STK runs, 0 guard fires, ~13 370 frames** (5 + 8 bench, 3 gate) versus the
row's baseline of **5 fires in one gate** three days earlier.

⚠ **A bound, not a clearance.** And an A/B against the pre-fix library would be **confounded**: any
revert relinks and changes layout, which is precisely what C1 is sensitive to, so a positive could not
be attributed to the `setvbuf` semantics rather than the shuffle. The documented way round it is a
**byte-identical** rebuild, which a functional revert cannot produce — so more exposure (unambiguous)
was chosen over an A/B (uninterpretable).

⚠ **I mis-reported this bench once first.** My wait exited while T6 was still running, so I graded a
mid-run snapshot and excluded T6 at "20 frames"; it actually rendered **623**. Nothing was pushed on
the bad read. **Fourth self-match variant tonight** — the fix, already in memory, is to wait on a pid
read off a real `ps` line, never on a process-name match. Three of tonight's measurement errors share
one shape: trusting a *derived* signal (a composed grep pattern, screen-rendered text, a process-name
heuristic) instead of the primary one the system actually emits.

## 0d. ★★★ 2026-09-25 — C1: the before/after boundary is clean (from archived logs, no new cycles)

Asked the question my 16-run bound depended on: **was the fires baseline still live, or had it gone
quiet days ago for unrelated reasons?** Answerable from disk.

* **Last archived guard fire: `20260924-160037-c1smoke`, 16:09 on 09-24**, `why=4` — and its record
  carries the documented signature: `heap = 0x800000010c9c5000` against `heapLo=0x2000
  heapHi=0x1a21b000`, i.e. **low half in range, upper 32 bits replaced by `0x80000001`**.
* **First libphoenix fix (`setvbuf`, `008e25d`) landed 22:54** — 6 h 45 m later.
* ⭑ **Zero STK-bearing logs between those timestamps**, so the boundary is not cherry-picked. The last
  STK exposure before the fixes **fired**; all 16 solid STK runs after them are **clean**.

⚠ **Still not causation.** Every fix relinks, and C1 is layout-sensitive, so this is equally
consistent with the `setvbuf` buffer-ownership repair or with a lucky layout. ➡ What it *does* settle
is that the baseline was **live ~7 h before the fixes**, not stale — so the 16-run bound measures
something real rather than a defect that had already gone quiet. That was the open question, and it is
now answered without spending a single Pi cycle.

## 0c. ⊗ 2026-09-25 — I refuted my own C1 correlation hypothesis

I twice floated that the `setvbuf` buffer-ownership repair might explain C1 going quiet. **It cannot.**
Disassembled the unstripped STK binary (6.26 M lines, 338 k `bl`): **0 call sites to `setvbuf`, 0 to
`freopen`**; control **1477** to `malloc`, so the probe demonstrably works. The V3D winsys is
in-process, so that binary *is* the whole STK process — the pre-fix bug **never executed** there.

⭑ **What STK actually exercises**, of the ten fixes: `sscanf` **96**, `strtol` 34, `strtoul` 13,
`strtoll` 7, `strtod` 4, `strtof` 3, `strftime` 3, `mktime` 1. Only **scanf** is reached at volume —
and its defects were a `%n` miscount, an unseen `0x` prefix, and an out-of-bounds **read** of
`buf[-1]`. ⚠ **None fits the signature**: C1 replaces the **high 32 bits of a 64-bit word with
`0x80000001`**, which requires a *write*; a one-byte OOB stack read cannot produce it.

➡ **Leading explanation for 0/16 is therefore LAYOUT** — the sensitivity this row already documents —
**not** any semantic fix of mine. Recorded explicitly so the next session does not inherit my wrong
hypothesis as a lead. The 16-run bound and the clean before/after boundary still stand; what changed
is that they no longer have a tempting causal story attached to them.

## 0b. 2026-09-25 — C4: window extended AND the documented reproducer actually run

Checked tonight's GPU load against C4 using the driver's own four literals (copied from source, not
composed): **0 occurrences across ~136 800 frames in 32 GPU-bearing logs** (3 gates + 16 STK runs).
Control: all 32 carry `v3d-winsys: flipstat`, so this is not a silent detector.

⭑ **But that bulk exposure did not test C4 at all.** The row names **`q3dm7`** as the reproducer; the
gates launch **`q3dm1`**. Extending a not-reproduced window with a map that was never implicated is
the same error class as G1's stale numbers — a real measurement that does not answer the question.

So a dedicated `quake3 +map q3dm7` cycle was run, and asserted to have loaded it (`Server: q3dm7`,
`trying to load maps/q3dm7.aas`) rather than trusting the frame count: **8 655 frames, 0 faults, 0
wedge signatures**. ⚠ One clean run cannot clear an intermittent — but it is the first exercise of the
*named* reproducer on a current shipping build, where the row's last sighting was 2026-08-22.

## 0a. 2026-09-25 — C5 and C8 extended from tonight's collected data

**C5 (PWM audio DMA stall):** graded the three gate Quake II runs with the *driver's own* literals
(`write STALLED`, `engine RECOVERED after the stall`, `paced null sink` — copied from `audio/`, not
composed). All three show `SDL audio initialized` **present**, so the audio path was genuinely
reached and the zeros mean something; **0** of each stall literal. Tally **28/28**. Conclusion
unchanged: the rate moves across neutral relinks, so this bounds rather than clears.

**C8 (NFS name resolution):** the `stat_nlink_size_blk_tim` group is in `test-libc-misc`
(`libc/misc/stat.c`), which ran in tonight's sweep — **212 Tests 0 Failures**, group cases present.
Tally now **1 firing in 91** runs.

Both are small, but both were free: the runs had already happened, and the only work was grading them
against the string the code actually prints.

## 09. ✅ 2026-09-25 — C2 reproducer re-run on today's build: fix holds

The cron asks for this every turn and I had been skipping it because C2 is **closed**. That reasoning
was weak: "closed" is the issue's status, not a guarantee the reproducer still behaves, and the
reproducer had not run since **2026-09-22 23:55** while libphoenix changed in ten places since.
For a *kernel DoS* that is the one failure class where a silent regression costs the whole system.

Rebuilt `stackbomb.c` from source against today's toolchain (not the prebuilt binary), staged, ran:

* **0 kernel faults** — no EL1 Data Abort, no panic
* **process dies as intended** — `Exception #36: Data Abort (EL0)`, thread 64, `/bin/stackbomb`
  (PID 25). Taken at **EL0**, not the EL1 double-fault of the original defect
* **system survives** — `/bin/ls /dev` and `/bin/uname -a` were appended *after* the bomb and both
  produced output; 7 `(psh)%` prompts

⭑ The third check is the one that could not be skipped: if the kernel had died the log would simply
**stop**, so "no error line" and "system fine" are indistinguishable unless something is made to run
afterwards. Recorded in the closed-issues archive.

## 08. 2026-09-25 — manifest snapshotted for the night's work

CLAUDE.md's rollback discipline says every validated step produces a `manifests/` entry recording all
sibling SHAs, and ~15 gated commits across four repos had gone in without one. Fixed:
**`manifests/2026-09-25-libc-ten-defect-cluster.md`** — all 17 siblings clean, libphoenix `ad351a3`,
tests `d0b75a9`, ports `8074f1b`, kernel `a6ddd2ca` (untouched).

That gives a single known-good point for the whole night, restorable with
`scripts/restore-integration-state.sh` rather than ad-hoc checkouts across four repositories.
