# W37 detail snapshot 5 — 2026-09-10 (morning)

Long-form detail trimmed out of `docs/inprogress/WEEK-2026-W37.md` to keep the weekly log short.
Nothing here is new work; it is the full write-up of items the weekly log now carries as one line.

---

## 2c. 🔎 Allocator double-free: much BROADER than this log said, and the key datum was never lost

**Two corrections to my own write-up, both material.**

**1. It is not an AF_UNIX-specific bug with 4 occurrences.** The report appears in **21 logs spanning
2026-06-28 → 09-09**, across **xcalc (×7), Dillo, vkQuake (×2), the glamor desktop / Window Maker /
showcase (×5)** and the liveness test (×4). Describing it as "an AF_UNIX liveness child" was far too
narrow — it is a **generic heap-corruption symptom** with several distinct causes, and several of
those were separately root-caused and fixed (the `vasprintf` overflow behind glib2/mc/Dillo, the
`free()`-of-`.rodata` in X11). Older ones say `Double free detected`; only the recent ones carry the
named report, which is why a grep for the new wording found nothing.

**2. The `size` field was NOT thrown away — it is in the archive.** `20260909-174804-getname1` has the
full five-line report:

```
malloc: double free() -- block already on the free list
  ptr = 0x25e0   size = 0x410   heap = 0x2000   hsize = 0x1000   hfree = 0xe40
```

`size = 0x410` = **1040 = PATH_MAX (1024) + CHUNK_OVERHEAD (16)**. So the double-freed block is one of
the two `malloc(PATH_MAX)` buffers in `resolve_path` — **the block class is now established from
evidence, not inferred.** It sits in the process's first, one-page heap, which was mostly free
(`hfree` 3648 of 4080 usable) at the time. That occurrence ran a **stock** libphoenix
(`7ad97f7`, ancestor of HEAD) — not an instrumented build.

**Ownership audit, done this turn: clean.** Every `resolve_path()` caller in libphoenix passes `NULL`
(so each owns its returned buffer); `resolve_path`'s own `path_copy`/`alloc_resolved_path` frees are
exactly-once on mutually exclusive paths; `unlink()`'s two `free(canonical)` are mutually exclusive;
and `sys_clear()` frees-and-NULLs the `sys_common` globals, so double-calling it is safe. No plain
logic double-free exists in the reachable paths.

✅ **Done instead of the ring buffer, and better: the report now names its CALLER** (libphoenix
`d875dc9`). `free()` reads `__builtin_return_address(0)` in its prologue and both report branches
print it, so the next occurrence says *which code* freed the block. **Zero cost on the normal path**
(one register read; only the report branches use it) — unlike a `resolve_path` ring buffer, which
would have put tracing in libc's hottest path for a demo build.

Verified end-to-end on hardware rather than just compiled: a probe that frees a PATH_MAX block twice
printed `caller= 0x4000c0`, and `addr2line -f -e dfprobe.elf 0x4000c0` → `main` at `dfprobe.c:19`,
which is exactly the second `free()`. The recipe is in the source comment, since installed binaries
are stripped and the address must be resolved against the unstripped `prog/` copy.
ⓘ One more constraint fell out of the probe: it reported `size=0xe41` (3648, **coalesced** with the
free remainder of its heap), where the real occurrence reported `0x410` (1040, **not** coalesced). So
in the real bug **both neighbours of the block were still in use** — the block had not merged. All three previously-named mechanisms are eliminated (duplicate
hand-out refuted over ~300 checked children; my bin-abandon regression fixed and postdating every
occurrence; server-overrun impossible on a path with no symlink component), so the remaining
candidates are ones only a live trace will separate.

ⓘ **And the non-recurrence tonight is weak evidence.** 21 occurrences across ~117 liveness-bearing
logs and months of app runs is a low rate; ~300 children in one night is consistent with seeing
nothing. It stays **open**, and I am no longer claiming tonight's fixes may have fixed it — none of
them addresses a mechanism that survived elimination.


## 4b. ⛔ Reel re-cut: examined and NOT worth doing — my earlier note overstated the case

The reel is **intact and verified**: 1920×1080 h264, exactly 247.000 s as documented, decodes
end-to-end with **zero** errors.

Last night I queued a re-cut on the grounds that its captions were "understated" versus the shipped
build. **Reading the segment list shows that was wrong.** The reel's Quake III caption says **46 fps**
and vkQuake **73 FPS** — both *higher* than anything measured tonight (Q3's first-person spawn view
read 25 fps). The reel's Q3 segment uses the showcase orbiting camera, a lighter view than a spawn
point. So the captions are not understated; they are measurements of **different moments of the same
workload** — the identical viewpoint/scene-complexity confound that stopped me calling STK a
regression. Re-capturing would produce different numbers, not demonstrably better ones, and could
easily produce **worse** footage.

Every caption is the engine's own on-screen readout, so all of them are true of the build that
produced them. Not re-cutting.


## 4e. ✅ Closed the trap that would have re-broken the X11 build

The Window Maker port's README still listed `nice()` as a "gap-fill no-op stub (no process-priority
API)" and pointed at gap-fill sources in the **coordination** repo under `tools/x11-port/ftw-phoenix/`
— a tracked **second copy** left over from the tools→ports migration that nothing builds from and
that still contained the stub. Following those docs is exactly how this morning's build break comes
back: libphoenix implements `nice()`, so a second definition is a hard link error that takes the whole
X11 showcase down. A reader would have edited the wrong file or re-introduced the collision.

Fixed: the gap table records `nice()` as provided by libphoenix with an explicit do-not-stub warning
and the linker error to recognise it by; it points at the live `files/ftw-phoenix/`; the duplicate
copy is retired (git keeps it — two copies of one source *is* the hazard); the coord status doc's gap
list marks `nice()` closed; and two source comments that referenced a path not reachable from them
now point at the co-located README. Verified the port still builds and `libftw.a` exports exactly
`alphasort ftw nftw scandir` — the four still genuinely missing from libphoenix.

## 4f. ✅ Swept every port shim for the collision class that broke two builds today

Both of today's build breaks were the same shape: a port ships a stopgap for a libc function, then
libphoenix grows the real one and the duplicate becomes a hard error (CPython's `clock_getres`,
Window Maker's `nice()`). So I swept **all 16** port compat/shim files against libphoenix's **994**
defined symbols.

**Exactly one overlap: `copysign` in vkQuake's SDL-compat glue** — whose own comment said its home was
libphoenix. Removed; the other 15 shims are clean.

⚠ **I predicted it was a latent break and it was not.** The glue compiles into an *archive* member,
so it is only extracted if something still needs a symbol from it, and vkQuake links and runs today
(it is in the gated image and passed the 6/6 gate). It is still worth removing — a duplicate strong
definition of a libc function makes the link's correctness depend on extraction order, which is
precisely how the `nice()` stub took the X11 build down.

ⓘ **And `build-port.sh` reports success without linking.** Its first two runs here compiled *nothing*
and proved nothing; only deleting the glue object, its archive **and** the binary forced a real
relink. Same stale-artifact trap as `libftw.a` this morning — a green port build is not evidence the
link was exercised. HW after the forced relink: vkQuake plays demo2, **0 faults**.

## 4g. ✅ MicroPython's `ticks_ms`/`ticks_us` were the WALL clock — fixed

A second live instance of the missing-POSIX-option-macro bug, found by the measurement above rather
than by guessing. Both functions probe
`_POSIX_TIMERS && _POSIX_MONOTONIC_CLOCK` and fell through to **`gettimeofday()`** — the wall clock,
which steps when ntpclient sets the time at boot. These are MicroPython's *monotonic* primitives:
`time.ticks_diff()` is the documented way MicroPython code measures elapsed time, so it returned
nonsense across a clock set.

Fixed (`phoenix-rtos-ports` patch `09_hal_ticks_monotonic`), HW-verified with a discriminator that
cannot be misread: `ticks_ms` now reads **17315** — 17.3 s since boot — where a wall-clock value would
be ms since the epoch (~1.78×10¹²); `ticks_us` agrees and `ticks_diff` over a 200 ms sleep returns
**exactly 200**.

⏭ The same measurement flagged **openssl** (`rand_unix.c:858` picks `CLOCK_REALTIME` for its entropy
timing). Left alone deliberately: it is entropy timing rather than an API contract, and openssl is
linked by many ports, so rebuilding it is a wide change for a small gain. Recorded rather than done.

