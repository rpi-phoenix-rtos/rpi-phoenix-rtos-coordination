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

---

## Appendix — the KNOWN-ISSUES C1 status cell as of 2026-09-25 (verbatim)

Preserved here before the row was compressed back to a summary, so nothing appended during
the night's work is lost. Paragraph breaks restored from the in-cell `<br><br>`; table pipe
escapes unescaped.

**Live but rare; root cause open.** ➡ **Full investigation record: [`docs/misc/2026-09-25-c1-dossier.md`](misc/2026-09-25-c1-dossier.md)** — moved there 2026-09-25 because this cell had reached 82k characters, 69 per cent of this file. **The one statistically supported fact:** `V3D_KEEP_CLOSED_BO=1` suppresses it (OFF 7/19, ON 0/12, Fisher p=0.019) — so the event needs closed-BO pages *recycled* into the kernel's general pool. ↩ **Corrected 2026-09-25 — the quarantine's negative is uninformative and must stop being quoted as a constraint.** It held ~1000 closed-BO pages and found nothing written to them, which was read as "the writer does not target the BO's old pages". But that same quarantine measured **0 fires in 5 runs**: in runs where the event never occurred, finding nothing on the held pages is exactly what you would expect *whatever* the writer targets. Its selftest (planting `0x80000001` at +4) proves the scan **can** see a write, not that one happened. A null detector result only constrains anything if at least one fire occurred in the same runs — none did. ➕ So the simplest explanation of the `KEEP_CLOSED_BO` result is back on the table: **the writer may well target the recycled BO pages themselves**, which is what that knob would prevent. ⬛ **Do not re-walk the eliminated set:** double BO ownership (measured clean, 0 overlapping reuses in 2827 events), application overrun, naming the `operator delete` caller (`-fomit-frame-pointer` makes it impossible), and the mailbox page-after-free mechanism (refuted — its timeout path never executes, and the site audit is now complete: all nine in-tree users are structurally safe, so there is no remaining release-by-`munmap` leak site). ★ **Signature:** the high 32 bits of a 64-bit word replaced while the low half stays intact, i.e. a 4-byte write at +4 of a page-aligned object. The two observed values, `0x80000000` and `0x80000001`, are exactly the two VideoCore property-mailbox response codes (success and parse-error), which the firmware writes to word[1] = +4 — and one victim was a **kernel** zone-list link, which no userspace use-after-free can reach. That equation still points at a device writing a physical address. ⚠⚠ **Measurement health (2026-09-25).** `malloc_dl.c`'s `C1-hunt: created` trace is four blocking `debug()` writes per victim-heap creation, up to 16 times — on the order of 200 ms of UART inside heap creation, in every process. It is now env-gated behind `C1_HEAP_TRACE=1`, default off (libphoenix `59c499a`), **proven both ways on hardware, workload held constant**: armed (`export C1_HEAP_TRACE=1`) → `created=14`, default → `created=0`, **both 1772 frames and 0 faults**. ↩ **RETRACTED the same night (libphoenix `d980ef3`): the claim that the trace was added *after* the last fire, and that no run carrying it had ever fired, was WRONG.** It came from globbing only `*stk*.log`, and every firing run of the 09-23/24 hunt is labelled `c1audit`/`c1cx`/`c1armA`/`c1master`/`c1off`/`c1smoke`, so the glob skipped them all. Measured across **all 852 GPU-bearing logs** (control: the log contains `flipstat`): trace **present 195 runs, 7 fired**; trace **absent 657 runs, 1 fired**. C1 fired **seven times with the trace compiled in**, most recently **2026-09-24 16:00**. The raw association points the opposite way to suppression and is confounded anyway — the trace was on precisely during the hunt sessions whose workloads were chosen to provoke the event. The gate stands only on its own merit: a 200 ms hot-path perturbation does not belong in a default build. ⛔ The poisons (`malloc_c1P4Poison`, the small-chunk payload poison) are the **detectors**, not reporting, and are constant across firing and non-firing runs — do not gate them. ➕ **Next:** re-establish a baseline on the default (un-traced) build; the one untested release path is **process exit while a property call is in flight**, which no leak counter sees. ⚠ That path cannot be checked from the logs: a c1base STK log has 0 exit/reap lines, but the kernel may simply not log exits, so 0 there is not evidence — it has to be *asked* of the system (a before/after process list around the run), not grepped for. ✅ **Detector positive control, 2026-09-25 (host, no Pi):** `tools/malloc-harness` rebuilt against today's `malloc_dl.c` (i.e. *with* the `C1_HEAP_TRACE` gate) still reproduces the exact field state — `why=5`, `hsize=0x8000000100001000`, `hlo32=0x1000`, `hhi32=0x80000001`, `hfixed=1`, and `free() of a corrupt chunk header` — on both the `hi32-write` and `orphan-uaf` experiments. So a 0-fire bench on this build is a **real negative, not a broken detector**. Run it before believing any run of zeroes. ➕ **Properly-measured current status (2026-09-25), and it says LESS than it looks.** Using `flipstat` as the control that a GPU run happened: C1 has fired in **8 of 793** GPU-bearing runs up to its last sighting (**2026-09-24 16:00**) and **0 of the 60 since**. ⛔ Do **not** read that as "it stopped", and do not use frame totals: the 8 firing runs produced **108 fires in only 20,901 frames** — one run alone had **78** — so fires **cluster within a run** and the per-frame view (206,148 clean frames since) massively overstates the evidence. The right unit is the **run**, matched for exposure. Every firing run rendered **1189–3407 frames, 7 of 8 above 2490**, so comparing only runs in that band: **before 7/422 (1.66%), after 0/31 — Fisher/binomial p≈0.59, i.e. entirely unremarkable.** Reaching p<0.05 at that rate needs **~179** comparable runs (~18 h of Pi time). ➕ **So the productive move is exposure per boot, not more boots:** tonight's baseline runs are ~1750 frames, *below* the band where almost every fire happened. Raise `--profile-laps` rather than multiplying short trials. The `V3D_BO_POOL` mitigation landed the same afternoon but is **opt-in** (verified off in these runs), so it is not the explanation either. ★★ **Where in a run the fires happen (2026-09-25) — detection is amplified by teardown.** All 78 fires of the biggest run land **after its last `flipstat`**, in STK's exit free-storm: the guards fire on `free()`, so a heap corrupted earlier is only *noticed* when something frees from it, and teardown re-walks everything. The two runs that reached STK's profile summary produced **79 of the 108 archived fires**. ⚠ But teardown is an **amplifier, not a precondition** — 6 of the 8 firing runs never got there and still fired 1–12 times, so a bench that does not complete is weaker, not void. ➕ Practical consequence: the historical firing runs used `--profile-laps=2` and needed **~454 s** of gameplay to finish; a 300 s capture window cuts STK off mid-race, which is what tonight's first baseline did. Give the cycle `--idle-secs 700` so the app completes **and** tears down. `scripts/c1-bench-table.sh` now prints a **TDOWN** column (`Number of frames:` = STK's profile summary) so this state is visible per trial instead of assumed. ↩ I first wrote that this *voided* the baseline; checking all 8 firing runs corrected that — it weakens it, and the earlier exposure-band comparison already captured the weakness. ✅ **Host allocator sweep, 2026-09-25 — the allocator itself is clean under randomized stress.** `tools/malloc-harness` built against today's `malloc_dl.c`, **400 fresh seeds × 120k ops with the injected experiments OFF**, so any violation would be a real defect: **0 violations** across **48.1 M checked ops, 7.45 **billion** chunks walked, 2.28 billion free chunks**, and mmap/munmap balanced to within 4 of 1,445,473. Host-only, ~25 min, no Pi time. ➕ That sweep did **not** cover concurrency — the harness's `--threads` defaults to 0, so its multithreaded stress never ran. Closed separately: **12 rounds × 16 threads × 1.5 M ops = 161.3 M allocs / 126.7 M frees / 34.6 M reallocs, 0 tag mismatches, 0 post-join invariant failures, 0 OOM.** This does not touch C1 — which needs a *device* write to a physical address and so cannot be reproduced on the host — but it removes the whole class of "the allocator corrupts itself" explanations, and it is the same technique that found 21 libext2 defects. ★★★ **2026-09-25 — the archived fires were re-read, and two long-standing characterisations are WRONG.** (a) ↩ **"always a `0xd000` bin-14 heap" is false.** `c1audit-t1` (09-23) reports `hsize=0x8000000100002000`, `hlo32=0x2000`, `hfixed=1` twice — a **`0x2000`** victim heap, clean in the log (the duplicate `0xe000…` line is UART corruption; `hlo32=0x2000` prints intact). ⚠ This matters because the `C1-hunt: created` instrument counts **only `0xd000`** creations, so the one counter we have is filtered to the wrong size and would never have shown the `0x2000` victim. (b) ↩ **The corrupted field is not always `heap->size`.** `c1smoke` (09-24 16:00, the most recent fire) has `why=4` and **`heap = 0x800000010c9c5000`** — the chunk's `heap` **pointer**, whose low half `0x0c9c5000` is the correct page-aligned heap containing `ptr=0x0c9ce008`. That write lands at **`chunk+12`, not page+4**, which independently confirms the archived warning that "C1 is at page+4" was only ever where the instrument looked. ➕ **What actually survives as invariant: the high 32 bits of a 64-bit word are replaced by `0x80000000` or `0x80000001` while the low half stays intact** — seen on `heap->size` (sizes `0xd000` *and* `0x2000`) and on a `chunk->heap` pointer, and matching the archived STK crash `far=0x800000010c845168`. Both values remain exactly the two VideoCore property-mailbox response codes. ⛔⛔ **2026-09-25 — RETRACTED: "C1 has gone quiet" was a counting error, and the poison guard gives the sharpest constraint yet.** Every "0 fires" figure I produced tonight counted only the `why   =` line, which belongs to **one** guard. The **page-poison** guard prints `POISON BROKEN` and no `why` at all. ↩ Recounting over every *guard message* was itself too coarse — it conflates distinct defects (the one ON-arm firing, `c1keep-4`, is `small-bin head is not a chunk` with `chunk=0x92fd8`, `hbase?=0`: **no high-half signature**, so not C1). The right selector is the **signature**: `0x80000000`/`0x80000001` in the high half, however reported (`hhi32`, `p4got`, a corrupted `hsize`/`heap`). By that measure **C1 is active and not quieter**: **24 of 793** GPU-bearing runs before 2026-09-24 16:00 carry it, and **4 of 65 since**. The most recent occurrence is **2026-09-25 09:14, in a quake3 `q3dm7` run — not SuperTuxKart**, so it is not STK-specific. (Tonight's 11 trials are genuinely clean on *all* guards, which at ~8% per run is unremarkable, p≈0.41.) ★★★ **And the poison guard is a direct observation of the writer, with two fields identical in 61 of 61 breaks across 19 logs, several days and two applications: `p4csize = 0x4ff0` and `p4got = 0x80000001`** — while the address varies (**24 distinct `p4page`, 21 distinct `p4chunk`**). So the target is not a fixed address but a fixed **allocation shape**: a *free* chunk of exactly `0x4ff0` receives a 32-bit `0x80000001` at the `+4` probe (`p4off = 0x4`, confirmed in the newer report format). `p4call` clusters inside ~1.6 KB (`0x1b2c7c4`–`0x1b2ce14`), though that is the **detecting** caller, not the writer. ✅ **And the `V3D_KEEP_CLOSED_BO` result SURVIVES this correction**: measured by signature across the whole archive with the arm asserted from the driver's own banner, **ON 0 of 20 runs, OFF 28 of 838** — the ON arm carries no poison breaks either, so the second guard does not overturn it. ➕ Next: identify the ~`0x4ff0` allocation — it is common to STK and quake3, so it is in shared code (Mesa/v3d winsys or libphoenix), not the game. ⚠ **Temper the 61/61 before over-reading it.** `_malloc_heapAlloc(max(lookup[idx], size))` sizes a large heap to the **bin's nominal size**, and `lookup[]` contains `0x3fff`: `CEIL(16 + 0x3fff, 4096) = 0x5000` exactly. So a `0x5000` heap is produced by *any* allocation falling in that bin — roughly **12–16 KB**, a common range — not by one distinctive object. The 61/61 agreement may therefore reflect that fully-free `0x5000` heaps are simply the **most numerous** poisoned large chunk, rather than that the writer selects them. The env-gated trace now covers `0x5000` (libphoenix `56e0cea`) and answers both halves at once: `c1call` names who allocates them, and the per-size counts say whether `0x5000` dominates. ➕ **Leading model, and the test the armed run supplies for free.** The poison has exactly one call site, `_malloc_chunkAdd` — used **both** when a chunk is freed **and** when a brand-new heap's chunk is binned. So a break cannot by itself distinguish *"a heap the app finished with, written afterwards"* from *"a heap just mmap'd onto pages the device still writes to"*. The second fits the one supported fact (`KEEP_CLOSED_BO=1` suppresses, and it is precisely what stops closed-BO pages being recycled) and fits `malloc_dl`'s own note that *"this region may be one we released earlier: mmap reuses addresses"*. ➕ **The armed `C1_HEAP_TRACE` run tests it with no extra instrumentation**: the trace logs `c1base` for every traced heap **creation**, and a break logs `p4page`. If a broken page falls inside `[c1base, c1base+0x5000)` of a heap created earlier in that same run, the victim was freshly created — which is the fresh-pages story; if it never does, it is the freed-heap story. ➕ **Signature rate by application** (whole archive, attributed by the app named most often in each log): **supertuxkart 27/552 = 4.9%**, quake3 **1/84 = 1.2%**, and **0** in quake2 (90), quakespasm (87) and vkquake (45). So "not STK-specific" is true but easily over-read: the quake3 sighting proves it is not a bug in STK's own code, yet **STK is ~4× the best reproducer and three other GPU apps have never produced it** — keep hunting on STK, and treat a quake3 sighting as a bonus rather than a second sampling channel. ★★★★ **2026-09-25 — the armed trace names the allocators, and the `0xd000` victim is Mesa's ralloc GC.** One armed `C1_HEAP_TRACE=1` STK run (binary `55a1c32f38a8d95f`, matching ELF archived first so `c1call` symbolizes honestly) gives per-size attribution: **`0xd000` — 3× `gc_zalloc_size`, 1× `operator new`, 1× `calloc`; `0x5000` — 3× `operator new`, 1× `_glapi_new_nop_table`; `0x2000` — 8× `operator new`**. `gc_zalloc_size`/`rzalloc_array_size` are **Mesa's ralloc garbage-collected allocator**, which independently corroborates the archived lead that the stray write is a Mesa **ralloc GC block header**. `c1req = 0x3fff` on every `0x5000` heap confirms the bin arithmetic exactly. ⚠ **Two limits, stated up front:** (a) `c1call` is `lastCaller` = `__builtin_return_address(0)` *inside* `malloc`, so for C++ paths it resolves to `operator new` and `-fomit-frame-pointer` blocks going higher — `operator new` is **not** an answer, only a wall; (b) the per-size budget is 8, consumed **early in the run**, so this characterises the heaps created first, not necessarily the ones corrupted later. The two manifestations may not share an allocator: the historical `hsize` victim is `0xd000` (ralloc GC) while the poison victim is `0x5000` (`operator new`). ★★★★★ **The armed run REPRODUCED C1 — and that kills the "instruments suppress it" worry for this instrument.** Same run: **SIG=52**, 13 × `free() of a corrupt chunk header`, `why=5`, 26 identical readings of **`hsize=0x800000010000d000`** (`hlo32=0xd000`, `hhi32=0x80000001`, `hfixed=1`), 3410 frames, teardown reached, 0 faults — **with `C1_HEAP_TRACE=1` armed and 21 trace reports emitted**. So this trace does **not** suppress the event, and its budget can safely be raised. ⚠ **The predicted limit bit exactly where flagged:** the victim heap is **`0x0b59a000`** while the traced heaps stop at `0x082b7000` — the per-size budget of 8 was spent on the earliest heaps, so the heap that actually got corrupted was never traced and its allocator is still unnamed. ➕ Next: raise the `0xd000` budget (that is the `hsize` victim size) far enough to reach heaps at `0x0b5…`, and re-run. ⛔ **Do not re-walk the ralloc lead — it is further along than it looks.** Reading the archive before running more: (a) the `0xd000` allocator was **already** identified as `gc_zalloc_size` (12 of 15 reports), so today's 3-of-5 only **replicates** it; (b) the tempting inference that `flags = IS_PADDING` with `slab_offset` 0/1 yields exactly `0x80000000`/`0x80000001` is **already retracted** — `flags ∈ {0,1,2,3}` and a real padding byte is `IS_PADDING | padcount` with `padcount ≥ 4` (`0x84`/`0x88`/`0x8c`), **never `0x80`**; (c) `gc_alloc_size`'s uninitialised `slab_offset` was checked and eliminated (every caller tests `bucket` first). ➕ **And the recorded "next step" is already implemented and shipping:** the plausibility check inside `get_gc_block_header()` (`bucket > NUM_FREELIST_BUCKETS`, or `slab_offset >= SLAB_SIZE` when `bucket < NUM_FREELIST_BUCKETS`) is in the binary (`strings` confirms) and has **never fired in any archived log — including the run that produced 52 signature hits**. ⚠ That null is **not yet evidence**: nothing showed `get_gc_block_header` is ever *reached*, and this hunt has already banked one such worthless null (the closed-BO quarantine). A one-line-per-process positive control was added (mesa `51c5ee977ba`, patch regenerated) so the next run separates "header always sane" from "function never called".

---

## Appendix 2 — KNOWN-ISSUES C1 status cell, later on 2026-09-25 (verbatim)

Second compression of the row. Everything appended between the first appendix and here is
preserved below; the row keeps a status summary.

**Open and ACTIVE; root cause open.** ➔ **Records: [hunt of 2026-09-25](misc/2026-09-25-c1-hunt-night.md)** (tonight, incl. the verbatim previous status cell) and **[the dossier](misc/2026-09-25-c1-dossier.md)** (everything before it). ★ **The invariant, and the only thing to quote:** the **high 32 bits of a 64-bit word are replaced by `0x80000000` or `0x80000001` while the low half stays intact** — seen on `heap->size` (sizes `0xd000` *and* `0x2000`), on a `chunk->heap` pointer at `chunk+12`, on a free chunk's poison at page+4, and on the STK crash `far=0x800000010c845168`. Both values are exactly the two VideoCore property-mailbox response codes. ⛔ **"Always a `0xd000` heap" and "always page+4" are both REFUTED** — do not reintroduce them. **Rate (by signature, not by guard name):** 24 of 793 GPU-bearing runs before 2026-09-24 16:00, **4 of 65 since** — no quiet period. By app: **STK 27/552 (4.9%)**, quake3 1/84, and **0** in quake2 (90), quakespasm (87), vkquake (45) — so hunt on STK. ✅ **One supported fact:** `V3D_KEEP_CLOSED_BO=1` suppresses it (0/20 by signature); it survived a full recount. ⛔ **Do not re-walk:** double BO ownership (measured clean); the mailbox page-after-free mechanism (all 9 in-tree sites audited structurally safe); the allocator itself (host sweep: 400 seeds / 7.45 bn chunks single-threaded plus 161 M allocs across 16 threads, 0 violations); and the ralloc lead — its allocator was already identified, its `flags = IS_PADDING` value theory is already retracted, and the designed `get_gc_block_header()` plausibility check already ships. ➕ **Open thread:** that check has never fired, and a positive control now proves the function *is* reached, so the null is becoming a real elimination — bounded by what the check can detect. ⚠ `c1call` resolves to `operator new` for C++ paths; `-fomit-frame-pointer` blocks going higher, so that is a wall, not an answer. ★★★★★ **2026-09-25, armed run `c1gc` (STK `4fcc4e2ee306ad5f`) — three results.** **SIG=308**, 3424 frames, teardown reached. **(1) The ralloc `get_gc_block_header` hypothesis is ELIMINATED.** A positive control proves the function **is reached** (`get_gc_block_header LIVE` printed), C1 was rampant in the same run (SIG=308), and the plausibility check fired **0** times — so a `gc_*` call is not walking back to a garbage header. Bounded only by what that check can detect (a garbage header could still pass both tests). **(2) ↩ The victim is NOT a whole `0x5000` heap — my own inference, now refuted.** Both `0xd000` (42 of budget 48) and `0x5000` (6 of 8) were traced **exhaustively**, budgets unexhausted, and the correlation reports **0 breaks inside any traced heap**. So the `0x4ff0` chunk at page-aligned+`0x10` is a **fragment** of a larger, untraced heap (legal sizes: `0x1000 0x2000 0x3000 0x4000 0x5000 0x7000 0x9000 0xd000 0x11000 0x19000 0x21000`), not the whole of a `0x5000` one. The 61/61 `p4csize=0x4ff0` agreement therefore means less than it appeared. **(3) C1 demonstrably crashes the application, in the allocator's own bin tree.** The run took an **EL0** Data Abort at **`lib_rbRemoveBalance`, `libphoenix/sys/rb.c:148`** (`far=0x8`, `esr=0x92000007`) — the red-black tree malloc uses for its **large bins**. ✅ The kernel survived: **0 EL1 faults**, psh alive afterwards. This is the clearest link yet from the stray write to real damage. ➕ **The crash is a C1 consequence, not an `rb.c` defect — checked, not assumed.** `rb.c:148` is `if (x == x->parent->left || …)` and `far=0x8` means `x->parent` was **exactly NULL** (reading `->left` at +8), reached via `nil.parent = parent` when `lib_rbRemove` passes both `node` and `parent` NULL. In a *consistent* tree that is unreachable: `rb_transplant` sets `rbtree->root = v` whenever the removed node is the root, so (a) root with no children → root becomes NULL → the `root == NULL` guard returns; (b) root with one child → `x == root` → the loop never runs; (c) non-root → `p` is `z->parent`/`y`/`y->parent`, all non-NULL. So the fault proves the bin tree was **already inconsistent**. ➕ Possible follow-up, matching the allocator's existing “leak rather than corrupt further” style (`abandoning the bin`, `leaking the heap`): a NULL guard here would turn this EL0 crash into a diagnosable report. ↩ But **not** "more evidence per run" — checked the ordering rather than assuming: the abort is at log line 2594 while STK's teardown summary is at **788**, so the game completed its whole run and died in **shutdown**, after the exit free-storm that produced all 308 signature hits. The guard is worth having for diagnosability, not for data volume. ★★★★ **2026-09-25 — every victim page in the whole archive lies in one ~100 MB window: `0x09a86000 … 0x0f9e8000`** (all 25 distinct `p4page` values, across 19 logs, several days and two applications). That is a much sharper targeting criterion than any count budget: tracing *every* heap `>= 0x5000` with budget 192 covered only **1 heap inside that window** before exhausting. ↩ *Corrected:* I first reported it as "stopped 43 MB short of the window", which was an artifact of requiring a **legal size** as well as a plausible base — that discarded 10 entries whose base was fine and whose size was UART-mangled, and those reach `0x0e3d5000`, i.e. **inside** the window. Right numbers: 154 with base+size plausible (max `0x08a3f000`), 10 more with base only (max `0x0e3d5000`), **164 total, of which 1 is in the window**. The count budget is still inadequate — but because it spends itself on early heaps, not because it cannot reach. ⚠ It also cost a **16% UART corruption rate** (30 of 184 reports had an implausible base or size, e.g. `0xe08590000`, `0x90e0`), so counts from heavy traces must be sanity-filtered before use. ➕ Next: replace the count budget with an **address window** — trace only heaps based in `[0x09000000, 0x11000000)` — which covers every archived victim with a fraction of the output and far less corruption. ➕ **Run `c1wide` (wide trace, 184 heaps, 3288 frames, teardown): SIG=0, 0 breaks, 0 guards** — an ordinary clean run at the measured few-%-per-run rate, and a reminder that the previous SIG=308 run was a heavy outlier. ➕ Two instrument lessons from it: (a) the `get_gc_block_header LIVE` control printed in the SIG=308 run but **not** in this one though the string is in both binaries — so Mesa's ralloc free path is **not exercised every run**, which leaves the previous elimination intact (there it *was* called, C1 was rampant, and the check stayed silent) but means the control must be re-checked per run; (b) `c1-bench-table.sh`'s GUARD column was counting the **trace's own output** (923 → 30 once excluded), i.e. my instrument was inflating the metric read beside it — fixed. ★★★★★ **The sharpest test available is now built: `p4pa`, the PHYSICAL address of a broken poison page** (libphoenix `6da559b`). The model says a **device** writes a physical address — the word is the VideoCore mailbox response code, and one victim was a **kernel** zone-list link no userspace UAF can reach. If so the writer keeps hitting **one physical page** and the 25 distinct virtual `p4page` values are just that page being recycled, so **`p4pa` should repeat while `p4page` does not**. If `p4pa` scatters as widely as `p4page`, the device-write reading is **wrong** and the hunt returns to a virtual-address writer. Verified callable first: `syscalls_va2pa` has **no privilege check** and resolves against the calling process's own pmap, and a poisoned free chunk is by definition mapped. ➕ If `p4pa` does repeat, the follow-on is already available: ask the driver which BO owned that physical page (`v3d_c1_lookup_page`), which would also explain why `V3D_KEEP_CLOSED_BO=1` suppresses — it is exactly what stops closed-BO pages being recycled. ✅ **Two run-mechanics wins, both verified on hardware.** (a) `/usr/bin/env C1_HEAP_TRACE=1 stk …` as a **single** psh command works — 95 heaps traced, so the variable propagates — which removes the second command and reclaims the ~12 min that `--idle-secs` (a **per-command** silence timeout) wasted on a trivial `export` every cycle. (b) The address window is doing its job: **92 of 93** traced heaps (99%) land inside it, and coverage of the tighter victim range went **1 → 5** heaps. ➕ But 5 is still thin, so the better lever is to stop guessing: the break path already holds `chunk`, so `chunk->heap` names the victim heap **directly** (libphoenix `684bc24` adds `p4heap`/`p4hsize`). ⚠ Read those two as evidence, not ground truth — a corrupted heap **pointer** is itself one of C1's recorded signatures, so a garbage high half in `p4heap` would be a finding in its own right.

---

## Appendix 3 — KNOWN-ISSUES C1 status cell, end of 2026-09-25 (verbatim)

Third compression. Preserved before the row was rewritten to remove two defects: it listed the
mailbox mechanism under "do not re-walk" while also presenting it as the leading hypothesis,
and an earlier edit left an orphaned sentence fragment.

**Open and ACTIVE; root cause open.** ➔ **Records: [hunt of 2026-09-25](misc/2026-09-25-c1-hunt-night.md)** (tonight, incl. two verbatim copies of this cell) and **[the dossier](misc/2026-09-25-c1-dossier.md)** (everything before). ★ **Invariant — the only thing to quote:** the **high 32 bits of a 64-bit word become `0x80000000` or `0x80000001`, low half intact** — on `heap->size` (`0xd000` *and* `0x2000`), on a `chunk->heap` pointer at `chunk+12`, on a free chunk's poison at page+4, and on the STK crash `far=0x800000010c845168`. Both values are exactly the two VideoCore property-mailbox response codes. ⛔ "Always `0xd000`" and "the victim is a whole `0x5000` heap" are **REFUTED**. ↩↩ **But "always page+4" is RE-ESTABLISHED — I refuted it in error.** I read the `chunk->heap` fire as landing at `chunk+12` and never checked where that falls: `CHUNK_OVERHEAD` is 16 and `ptr = chunk + CHUNK_OVERHEAD`, so `ptr=0x0c9ce008` → `chunk=0x0c9cdff8` → `&chunk->heap = 0x0c9ce000` (**page-aligned**) → its high half at **`0x0c9ce004` = page+4**. ★★ **All three detection paths agree, and two of them are independent of the poison instrument** — so page+4 is no longer "where the instrument looks": (a) `heap->size`'s high half is at `heap+4`, and heaps are mmap'd page-aligned; (b) the `chunk->heap` case above; (c) the poison at `p4off=4`. ➕ That yields a single unified model: **the writer stores a 32-bit `0x8000000x` at PAGE+4, and we only notice on the pages where page+4 happens to hold something validated**. ⚠ **Bounded by `tools/memtrip`**, which watched page+4 across 64 MiB of anonymous pages in a **separate process** and never saw it: so the targets are **not arbitrary pages** — they are specific ones, consistent with the victim window and with pages that were recently something else (the `KEEP_CLOSED_BO` result points at recycled BO pages). "Page+4 of many pages" would contradict memtrip; "page+4 of the pages it targets" does not — a heap size's high half, a `chunk->heap` high half, or a poison word. It also explains the apparent chunk-alignment coincidence: only a chunk sitting at page−8 puts its `heap` field's high half on page+4. **Rate (by signature, never by guard name):** 24/793 GPU runs before 2026-09-24 16:00, 4/65 since; by app **STK 27/552 (4.9%)**, quake3 1/84, **0** in quake2/quakespasm/vkquake — hunt on STK. **Impact:** C1 crashes STK inside malloc's own large-bin rbtree (`lib_rbRemoveBalance`, `sys/rb.c:148`) during *shutdown*; the **kernel survives** (0 EL1 faults). ✅ **One supported fact:** `V3D_KEEP_CLOSED_BO=1` suppresses (0/20 by signature). ⛔ **Do not re-walk:** double BO ownership; the mailbox page-after-free mechanism (all 9 sites audited safe); the allocator itself (400 seeds / 7.45 bn chunks, plus 161 M allocs / 16 threads, 0 violations); and **ralloc `get_gc_block_header` — ELIMINATED** with a positive control proving the function is reached while C1 was rampant and the check stayed silent. ➕ **Live experiment:** `p4pa` tests the device-write model (a repeating **physical** address while virtual ones scatter would confirm it); `p4heap`/`p4hsize` name the victim heap directly; the trace is address-window filtered to where all 25 archived victims live; runs use `/usr/bin/env C1_HEAP_TRACE=1 stk …` as one command. Grade every run on **SIG**, **TDOWN** and **LIVE** — each has misled once. ⚠ Five measurement retractions tonight, every one from a **too-narrow selector**; select on the property, and check known positives survive the filter. ★★★★★ **2026-09-25 — address AND value both match a VideoCore mailbox response, independently.** Every mailbox buffer in this system is an mmap'd **page** (`msg_page = mmap(NULL, _PAGE_SIZE, …); msg = msg_page` in `v3d_phoenix_power.c`; `vcmbox.buf = buf_page` in `rpi4-vcmbox`), and the firmware's response code goes to **`msg[1]`** — i.e. **page+4**. So C1's *address* (page+4, now established from three detectors, two independent of the poison probe) and its *value* (`0x80000000`/`0x80000001`, exactly the success and parse-error response codes) are **both** exactly what the firmware writes into a property buffer. ⚠ **This does not overturn the earlier refutation, it relocates it.** That refutation rested on `mboxProp`'s `leak` counter reading 0, which only covers the **post-doorbell timeout release**. The coincidence says the *mechanism* is right and our model of the **release path** is wrong. Paths the counter cannot see: **process exit while a call is in flight** (the kernel reclaims with no `munmap` and no counter — still untested), or a coherency window where the FIFO reply is observed before the buffer write lands and the page is unmapped in between. ➕ **Decisive next test, already half-built:** `p4pa` gives the **physical** address of a broken page; log the PA of each mailbox request buffer and compare. A match names the mechanism outright. ⚠⚠ **State the tension honestly: a positive control for this very mechanism already FAILED.** `V3D_MBOX_LEAKTEST` (compiled out unless `V3D_C1_HUNT`) returns the message page to the kernel **before** ringing the doorbell, so VideoCore is *guaranteed* to reply into a page the kernel has already reclaimed — the mechanism, forced. Its own comment set the bar: *"if this still does not raise C1 above the baseline, the mechanism cannot produce the signature and the candidate is dead."* Measured **leak 0/3 vs control 1/3** — it did **not** raise the rate. ➕ So two strong facts point opposite ways: the **address+value coincidence** (page+4 and the two response codes, both independently derived) argues the firmware *is* the writer, while a **forced** leak of exactly that kind produced nothing. Possible resolutions, in order of how cheaply they can be tested: (a) n=3 is far too small — at today's measured few-%-per-run rate a 3-run arm could not have shown an effect either way, so the control is **underpowered, not negative**; (b) the forced leak differs from the real one (a page freed pre-doorbell may be re-mapped differently from one freed after a *successful* call); (c) the coincidence is real but the writer is a different VideoCore agent that reuses the same response encoding. ⛔ Do not quote the control as a refutation without noting (a) — and the `p4pa` comparison now settles it directly, without needing either.

