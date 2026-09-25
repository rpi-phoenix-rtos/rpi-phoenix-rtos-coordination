# C1 hunt — night of 2026-09-25

Moved out of `docs/inprogress/WEEK-2026-W39.md` to keep the weekly log readable: this one night's
C1 narrative had grown to ~317 lines. The weekly log keeps the at-a-glance table and a summary;
everything below is that section verbatim, newest first.

⚠ Read the corrections: several of my own measurements here were wrong first and are retracted
**in place** rather than deleted, because each retraction is the useful part.

### ★★★ C1: we have been measuring a build with ~200 ms of UART inside heap creation

Goal #2 for the night. I did **not** add an instrument — I found one and turned it off.

`malloc_dl.c` prints `C1-hunt: created a 0xd000 heap` plus three hex lines on **every** creation of
the victim heap size, up to 16 times. That is ~64 blocking `debug()` writes; at 115200 baud, on the
order of **200 ms of UART inside heap creation, in every process**, at exactly the moment the victim
heap is born.

↩ **RETRACTED the same night — my evidence for this was wrong.** I claimed the trace was added
*after* the last fire and that no run carrying it had ever fired. That came from globbing only
`*stk*.log`; every firing run of the 09-23/24 hunt is labelled `c1audit`/`c1cx`/`c1armA`/
`c1master`/`c1off`/`c1smoke`, so the glob skipped all of them. Measured properly over **all 852
GPU-bearing logs** (control: the log contains `flipstat`):

| build | runs | fired |
|---|---|---|
| trace **present** | 195 | **7** |
| trace **absent** | 657 | 1 |

C1 fired **seven times with the trace compiled in**, most recently **2026-09-24 16:00**. The raw
association points the *opposite* way to suppression, and is confounded anyway — the trace was on
precisely during the hunt sessions whose workloads were chosen to provoke the event.

**The gate still stands, on the ground it should always have rested on:** four blocking `debug()`
writes per creation, up to 16 times, is ~200 ms of UART inside heap creation in every process, which
does not belong in a default build whatever it does or does not suppress. Retraction in libphoenix
`d980ef3`.

⚠ Consequence for tonight's baseline: its premise ("restore the un-traced baseline") was
misconceived. The 0-fire result is still a real negative on the current build — the detector was
proven live on the host — but it says nothing about the trace.

**Done and proven both ways on hardware**, workload held constant:

| arm | `created` | frames | faults |
|---|---|---|---|
| `export C1_HEAP_TRACE=1` | **14** | 1772 | 0 |
| default (unset) | **0** | 1772 | 0 |

The pre-`main` latch risk — a 0xd000 heap created by a C++ static constructor before `environ`
exists would latch the trace off for the whole process — did not materialise.

Env-gated behind `C1_HEAP_TRACE=1`, default off, read with `getenv` inside the *one*
binary (`_env_find` is NULL-safe, never allocates, takes no allocator lock) so the arms stay
env-selected instead of becoming two binaries. The decisive reporters (`hlo32`/`hhi32`/`hfixed`) are
untouched — all four call sites are guard-fire paths that cost nothing on a healthy run.

**Interim baseline result: 0 fires / 6 valid trials** (10,492 frames, 0 faults, 0 void). Read
exactly as pre-registered below: P(0 fires in 6 | historical 7/19 rate) = 0.632⁶ ≈ **0.064** —
suggestive, **not** conclusive, so trials 7–12 are running. The trace is *not* yet shown to be the
suppressor.

**Teardown-reaching series: 4/4 trials completed and tore down** (3379–3451 frames each, in the
historical firing band), **0 events on every guard**. At the measured ~3–6% signature rate that is
unremarkable, so it neither confirms nor refutes anything — it is four properly-exposed trials on the
board, which the earlier 300 s-window runs were not.

➡ **Now chasing the `0x5000` lead directly rather than by more trials.** `heapSize = CEIL(16 + size,
4096)`, and a large heap is sized to the **bin's nominal size** (`max(lookup[idx], size)`).
`lookup[]` holds `0x3fff`, and `CEIL(16 + 0x3fff, 4096) = 0x5000` exactly — so a `0x5000` heap comes
from *any* allocation in that bin, roughly **12–16 KB**.

⚠ That tempers the 61/61: it may mean fully-free `0x5000` heaps are simply the **most numerous**
poisoned large chunk, not that the writer selects them. The trace answers both halves — `c1call`
names the allocator, and the per-size counts say whether `0x5000` dominates.

⚠ **Caught a flaw in my own experiment before spending the run on it.** The trace's report cap was
**one shared pool of 16**, and historical runs already reported 13–16 creations with the `0xd000`-only
filter. With three sizes sharing that budget, whichever size is created first consumes it — so
`0x5000`, the size the trace had just been widened for, could have gone unreported in **every** run.
An instrument that can silently omit the thing it was widened to find is worse than none. Budget is
now **8 per size** (libphoenix `9829f02`); the port build was stopped and restarted rather than
spending 40 minutes producing an answerless run. Grepping the v3d driver for it found nothing, so rather than guess I extended
the env-gated trace to `0x5000` (libphoenix `56e0cea`) — it prints `c1call`, which names the
allocating caller. Rebuild is queued behind the running series; STK must be **relinked**, since the
trace is in libphoenix and `--scope core` alone leaves the port stale.

**Next run is armed (STK `4fcc4e2ee306ad5f`, matching ELF archived first). Interpretation fixed in
advance:**

| observation | reading |
|---|---|
| `get_gc_block_header LIVE` **absent** | the function is never reached in STK ⇒ the ralloc-GC-header hypothesis is **moot for this workload**, and the `gc_zalloc_size` attribution means only that ralloc *allocates* the victim heaps, not that it corrupts them |
| LIVE present, `implausible gc header` = 0 | the function runs and never returns a bad header ⇒ a real **elimination** of "a `gc_*` call on a non-GC pointer walks back to garbage" — bounded by what the check covers, since a garbage header could still pass both tests |
| `implausible gc header` > 0 | **direct catch**; `ret=` names the caller region |

⚠ Whatever comes back, `c1call` still resolves to `operator new` for C++ paths — that wall does not
move.

★★★★★ **The armed run REPRODUCED C1, and named the allocators — the best result of the night.**

One `C1_HEAP_TRACE=1` STK run on binary `55a1c32f38a8d95f` (matching unstripped ELF archived
*before* the run, so `c1call` symbolizes honestly): **SIG=52**, 13 × `free() of a corrupt chunk
header`, `why=5`, 26 identical readings of **`hsize=0x800000010000d000`**, 3410 frames, teardown
reached, 0 faults.

✅ **That kills the "instruments suppress it" worry for this instrument** — it emitted 21 trace
reports and the event still fired 52 times. Its budget can therefore be raised safely.

**Per-size allocator attribution:**

| heap size | allocating caller |
|---|---|
| `0xd000` — the `hsize` victim size | **3× `gc_zalloc_size`**, 1× `operator new`, 1× `calloc` |
| `0x5000` — the page-poison victim | 3× `operator new`, 1× `_glapi_new_nop_table` |
| `0x2000` | 8× `operator new` |

`gc_zalloc_size`/`rzalloc_array_size` are **Mesa's ralloc GC allocator**, corroborating the archived
lead that the stray write is a **ralloc GC block header**. `c1req=0x3fff` on every `0x5000` heap
confirms the bin arithmetic.

⚠ **The predicted limit bit exactly where flagged.** The victim heap is `0x0b59a000`; the 8 traced
`0xd000` heaps stop at `0x082b7000`. The budget went to the earliest heaps, so *the heap that
actually got corrupted was never traced* and its allocator is still unnamed. Budget for `0xd000`
raised 8 → 48 (libphoenix `05b2654`), rebuild running.

⚠ Also: `c1call` is `lastCaller` from inside `malloc`, so C++ paths resolve to `operator new` and
`-fomit-frame-pointer` blocks going higher. **`operator new` is a wall, not an answer.**

⚠ Harness trap: `--idle-secs` is a **per-command** silence timeout, so a trivial `export` as command
1 burned a whole 700 s window before `stk` was sent. The run recovered (the window is per-command)
but lost 12 minutes. Use `--ready-line`, or budget `n_commands × idle_secs`.

⛔⛔ **RETRACTED: "C1 has gone quiet" was my counting error — and the guard I missed is the best
lead of the night.**

Every "0 fires" number I produced tonight counted only `why   =`. That line belongs to **one** guard.
The **page-poison** guard prints `POISON BROKEN` and no `why`. Recounted over all guard messages:

↩ …then counting *every guard message* proved too coarse in the other direction: it conflates
distinct defects. The one `KEEP_CLOSED_BO=1` firing (`c1keep-4`) is `small-bin head is not a chunk`
with `chunk=0x92fd8`, `hbase?=0` — **no high-half signature**, so not C1 at all.

**The right selector is the signature**, not the guard name: `0x80000000`/`0x80000001` in the high
half, however it is reported (`hhi32`, `p4got`, a corrupted `hsize`/`heap`). By that measure:

| | runs | with C1 signature |
|---|---|---|
| before 2026-09-24 16:00 | 793 | 24 |
| since | 65 | **4** |
| `KEEP_CLOSED_BO=1` | 20 | **0** |
| default arm | 838 | 28 |

**C1 is active and no quieter** — and ✅ the `V3D_KEEP_CLOSED_BO` suppression result **survives**:
the ON arm carries no poison breaks either, so the guard I had missed does not overturn it. The latest occurrence is **today, 09:14, in a quake3 `q3dm7` run —
not SuperTuxKart**, so it is not STK-specific either. Tonight's 11 trials are genuinely clean on
*all* guards, which at ~8%/run is unremarkable (p≈0.41). `c1-bench-table.sh` now counts every guard;
it had called `c1armA-3` clean when that run had 4 poison breaks.

★★★ **The poison guard is a direct observation of the writer** — and two fields are identical in
**61 of 61** breaks, across 19 logs, several days and two applications:

```
p4off   = 0x4                p4got   = 0x80000001     (expected poison, e.g. 0x7db04d01)
p4csize = 0x4ff0             p4chunk/p4page vary: 21 / 24 distinct values
```

So the target is **not a fixed address** but a fixed **allocation shape**: a *free* chunk of exactly
`0x4ff0` takes a 32-bit `0x80000001` at its page+4 probe. `p4call` clusters inside ~1.6 KB, but that
is the **detecting** caller, not the writer.

➡ **Next:** identify what allocates ~`0x4ff0`. It is common to STK *and* quake3, so it lives in
shared code — the Mesa/v3d winsys or libphoenix — not in either game.

★★★ **I re-read the archived fires themselves — and two long-standing characterisations of C1
are wrong.** The recent firing runs carried the decisive instruments; nobody had read them back.

| run | why | corrupted field | high half | low half |
|---|---|---|---|---|
| `c1smoke` (09-24 16:00, latest) | 4 | chunk→`heap` **pointer** | `0x80000001` | `0x0c9c5000` |
| `c1off` / `c1master` / `c1armA` | 5 | `heap->size` | `0x80000000` | `0x0000d000` |
| `c1audit` | 5 | `heap->size` | `0x80000001` | **`0x00002000`** |
| `c1both` | 8 | heap intact (`hhi32=0`) | — | other guard |

↩ **"Always a `0xd000` bin-14 heap" is false.** `c1audit` shows a **`0x2000`** victim
(`hlo32=0x2000`, `hfixed=1`, printed twice; the `0xe000…` duplicate is UART corruption). ⚠ And the
`C1-hunt: created` counter only counts **`0xd000`** creations — so our one exposure counter is
filtered to the wrong size and could never have seen this victim.

↩ **The corrupted field is not always `heap->size`.** The latest fire corrupts a chunk's `heap`
**pointer** at `chunk+12` — not page+4 — independently confirming the archived warning that
"C1 is at page+4" was only ever where the instrument looked.

➕ **What survives as the real invariant:** the high 32 bits of a 64-bit word are replaced by
`0x80000000` or `0x80000001`, low half intact — across `heap->size` (both `0xd000` and `0x2000`), a
`chunk->heap` pointer, and the archived STK crash `far=0x800000010c845168`. Both values are still
exactly the two VideoCore property-mailbox response codes.

✅ **Two 2/8 GB mailbox holes closed** (devices `ec654c0`, helps **P1**). Chasing C1's
"device writes a physical address" equation, I checked how each mailbox caller computes the address
it hands VideoCore. `rpi4-vcmbox` rejects a PA that is unresolvable **or above 4 GiB**, with a
comment describing the exact failure mode; `rpi4-wifi` does too. The two in-process v3d callers —
one of them linked into **every GPU app** — checked only the unresolvable case and then cast to
`uint32_t`, silently truncating. Now guarded.

⚠ **Not a C1 fix, and recorded as not being one.** On this 4 GB board the truncation is latent: the
highest PA anywhere in the archive is ~`0x451f0000` and vcmbox's equivalent guard has never fired in
any archived boot. I checked that before writing it up rather than after.

✅ **Host allocator sweep: 400 seeds, 0 violations.** `tools/malloc-harness` against today's
`malloc_dl.c`, 400 fresh seeds × 120k ops with the injected experiments **off** so any violation
would be a real defect:

| | |
|---|---|
| checked ops | 48,109,035 |
| chunks walked | **7,447,650,288** |
| free chunks | 2,280,338,250 |
| mmap / munmap | 1,445,473 / 1,445,469 |
| **violations** | **0** |

~25 min, host-only, no Pi time. It cannot reproduce C1 — that needs a *device* write to a physical
address — but it removes the entire "the allocator corrupts itself" class, and it is the technique
that found 21 libext2 defects.

⚠ Then I noticed that sweep covered **no concurrency at all**: the harness has a multithreaded
stress, but `--threads` defaults to 0, so 400 seeds of single-threaded work said nothing about the
locking. Closed it — 12 rounds × 16 threads × 1.5M ops:

| allocs / frees | reallocs | tag mismatches | invariant failures |
|---|---|---|---|
| 161,290,776 / 126,718,056 | 34,572,720 | **0** | **0** |


★★ **Where in a run the fires happen — and a correction I made to myself mid-analysis.**

All 78 fires of the biggest run land **after its last `flipstat`**, in STK's exit free-storm. That
makes sense: guards fire on `free()`, so a heap corrupted earlier is only *noticed* when something
frees from it, and teardown re-walks everything. The two runs that reached STK's profile summary
produced **79 of the 108 archived fires**.

I immediately concluded this **voided** tonight's baseline, since those runs are cut off at 300 s and
never tear down. Then I checked all eight firing runs rather than the two convenient ones:

| reached teardown | runs | fires |
|---|---|---|
| yes | 2 | 79 |
| **no** | **6** | **29** |

So teardown is a detection **amplifier, not a precondition** — 6 of 8 firing runs never got there and
still fired 1–12 times. The baseline is *weaker*, not void, and the exposure-band comparison already
captured that weakness.

➡ Practical: the firing runs used the same `--profile-laps=2` but needed **~454 s** to finish; a
300 s window cuts STK off mid-race. New series runs with `--idle-secs 700` so the app completes and
tears down. `c1-bench-table.sh` now prints a **TDOWN** column so this is visible per trial instead of
assumed — proven both ways (78-fire run `yes`, baseline run `no`).

➕ **And the measurement that should have been made first — which says less than it looks.**

Using `flipstat` as the control that a GPU run happened: C1 fired in **8 of 793** GPU-bearing runs up
to its last sighting (2026-09-24 16:00), and **0 of the 60 since**. Tempting to call that a stop. It
is not, for two reasons I had to work through:

1. **Fires cluster inside a run.** The 8 firing runs produced **108 fires in 20,901 frames** — one
   run had **78**. So the per-frame view (206,148 clean frames since) overstates the evidence
   enormously; the run is the right unit.
2. **Match the exposure.** Every firing run rendered **1189–3407 frames, 7 of 8 above 2490**.
   Comparing only that band:

| window | runs ≥2490 frames | fired |
|---|---|---|
| before | 422 | 7 (1.66%) |
| since | 31 | **0** |

  p≈0.59 — entirely unremarkable. Reaching p<0.05 at 1.66%/run would need **~179** comparable runs,
  about 18 hours of Pi time.

➡ **So the productive move is exposure per boot, not more boots.** Tonight's baseline runs are
~1750 frames — *below* the band where almost every fire happened. The next hunt should raise
`--profile-laps` rather than multiply short trials.

✅ **Detector positive control first — the zeroes are real.** Before drawing anything from a run of
zeroes I rebuilt `tools/malloc-harness` against today's `malloc_dl.c` (with the gate in it) and ran
it on the host: it still reproduces C1's exact field state — `why=5`,
`hsize=0x8000000100001000`, `hlo32=0x1000`, `hhi32=0x80000001`, `hfixed=1`, `free() of a corrupt
chunk header` — on both `hi32-write` and `orphan-uaf`. So the detection path is intact and a
0-fire bench means 0 events, not a dead guard. No Pi time.

⚠ **The pre-registration below is kept for the record but its premise is void** — see the
retraction above. The baseline is still worth having as the current build's rate with a
host-proven-live detector, but "un-traced vs traced" is not a contrast the data supports.

★★ **And a third, which re-opens an avenue that was closed on bad grounds.** The closed-BO
quarantine held ~1000 pages, found nothing written to them, and that was recorded as *"the writer
does not target the BO's old pages"* — a constraint that has steered the search ever since.

But that quarantine measured **0 fires in 5 runs**. A detector that finds nothing during runs in
which the bug never fired says nothing about what the writer targets; its selftest (planting
`0x80000001` at +4) proves the scan *can* see a write, not that one happened. So the simplest
explanation of the `KEEP_CLOSED_BO` result is back on the table: **the writer may target the
recycled BO pages themselves**, which is exactly what that knob prevents.

★ The general rule, now in the C1 memory: *check the fire count of the very runs a negative came
from before letting it eliminate anything.*

★ **Two findings that re-point the search:**

1. `0x80000000` / `0x80000001` are **not** "a refcount with an MSB flag" — they are exactly the two
   VideoCore property-mailbox response codes (success, parse-error), which the firmware writes to
   word[1] = **+4** of the request buffer. The row's weaker reading is corrected.
2. The mailbox-site audit is now **complete**: all nine in-tree users are structurally safe.
   `rpi4-thermal` and `bcm2711-sdio` were never audited before and go through `libvcmbox`, whose
   server builds every message in its **own** persistent bounce buffer, so a client buffer never
   reaches the firmware; `bt/rpi4-hci`, `pcie/server` and `plo/video.c` use **unbounded** waits. So
   "no remaining leak site" is established rather than assumed, which pairs with the already-recorded
   failed positive control to make the mailbox mechanism a firm negative **for release-by-munmap**.
   ➡ Still untested: **process exit while a property call is in flight** — no leak counter sees it.

⚠ Also caught by the strings check: a libphoenix change does **not** invalidate a port's build
state. The first `--scope core` left the shipped STK byte-identical (`be44a168`) without
`C1_HEAP_TRACE`; the port needs `build-port.sh supertuxkart`.

Housekeeping: the C1 and D9 rows had `|` inside code spans and inside prose, which silently split
them into extra table columns — both repaired, and no malformed rows remain.
