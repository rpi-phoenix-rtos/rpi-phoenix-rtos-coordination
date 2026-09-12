# Allocator free-bin corruption — the 2026-W37 investigation

*Archived from WEEK-2026-W37 on 2026-09-12. The defect is still open; this is the working
detail so the weekly log can stay short. Current state is summarised there.*

## 5. Allocator bin corruption — CONTAINED, first cause open, hunting stopped

A free bin ends up holding a pointer to memory that is not what the bin thinks it is. Proven by
in-allocator verdicts rather than inference (`hbase?=1` on 24/25, 34/34, 19/19 and 3/3 events).

✅ **Contained on both hand-out paths** (`0eab703`, `9aaacdf`): a **22-event run finishes with 0
exceptions and 1301 frames**, where this used to end in a Data Abort inside `malloc_chunkSize()`.
That is the part that matters for a demo — the defect no longer ends runs.
✅ **Regression-tested twice**: 541 libc tests, 0 failures, and **zero spurious instrument fires**.

★ **2026-09-12 — ruled out a cause for free (no Pi time).** Both this and STK's out-of-bounds
drive-graph index were only ever seen in SuperTuxKart, so the graph guards were the obvious
candidate. Classifying every STK run by timestamp against those commits: **before — 208 runs, 10 with
events (4.8%); after — 36 runs, 3 with events (8.3%)**. No drop ⇒ **independent defects**. Current
rate ~3 in 36 runs, and **containment holds** (22 events / **0 faults** / 1301 frames; 19 / 0 / 1310).
★★ **2026-09-12 — found the first-order event, and it REFUTES the standing theory.**
`malloc_reportBadNeighbour()` fires before the bin damage, and its reports were already sitting in
the logs: **11 events, 7 runs, all STK.** In **11 of 11** the derived `sibling` is **page-aligned**,
while the `chunk` it came from is only 16-byte aligned (**0 of 11**). `sibling = chunk + size`, so
random bytes over a size/footer would land page-aligned with p≈1/256 — **11/11 is ~1e-27**. So the
walk hits a page boundary **by construction**, and the page-aligned objects here are **heap bases and
ends**. One run had **five events with the SAME sibling** (`0x0d12a000`) from five chunks in **three
different heaps** — the signature of a wrong `chunk->heap`, not of random corruption.
⇒ **It is not a userspace buffer overflow** (the theory written in the source). Aim at
`malloc_chunkIsLast()` failing to stop the walk: a chunk's `->heap`, or that heap's `->size`,
disagreeing with its real heap. Evidence recorded beside the reporter in `malloc_dl.c`.
⏳ **A discriminator is now armed and running.** `malloc_chunkValid()` checks the sibling against the
**original** `chunk->heap` captured at join entry, while `malloc_chunkIsLast()` uses **`it->heap`** —
so if the backward loop promoted `it` into a different heap, this report fires with **no corruption
anywhere**. The reporter now prints `refheap` (the heap validation used), `chunksz`, `heapsz` and
`heapEnd`, so the next event separates the two outright: **`refheap != heap` ⇒ a heap mismatch, not
damage; `refheap == heap` ⇒ a real boundary overrun.** `heapsz` is only read when the pointer passes
the same window test `malloc_chunkValid()` uses, so a bogus `->heap` cannot fault the reporter.
★★ **2026-09-12 — found the gap and fixed it.** `malloc_chunkValid()` checks that a chunk's
**address** lies inside the given heap; it **never checks that the chunk's own `->heap` field
agrees**. So a chunk can pass validation carrying a wrong `->heap`, and `malloc_chunkIsLast()` then
derives the heap end from that wrong pointer — landing past the real end, answering "not last", and
stepping the walk to `chunk+size` = **the real heap's end, page-aligned**. That accounts for the
whole 11/11 fingerprint. **Fix** (libphoenix **`165439a`**): `_malloc_chunkJoin()` computes the
boundary from the **trusted reference heap** (the caller's `chunk->heap`, which every promoted `it`
was validated against) instead of `it->heap`. When `->heap` is correct the two are identical, so
healthy behaviour is unchanged. **Verified: 513 libc tests (93+212+208), 0 failures, 0 faults.**
Manifest `2026-09-12-malloc-join-trusted-heap.md`.
**Validated on STK, 10 trials: 0 first-order events, 0 bin events, 0 heap mismatches.**
⚠ **Not proof the fix eliminated it** — at the measured ~7%/run, 0 in 10 is a **48%** outcome. What
it does establish is no regression: STK still runs, and the new detector never fired, so no promoted
chunk carried a disagreeing `->heap` in these runs.
🐞 **STK still crashes ~1 in 10 — and the drive-graph guards did NOT end it.** Symbolised: the
faulting site is **`FontWithFace::render(...)`**, STK's font renderer, not the AI path. The same
`far=0x80000001_<heap ptr>` signature occurred on 09-08, 09-09, **09-11 03:05** and **09-12 07:04** —
and the guards landed 09-11 **05:36**/**07:56**, so the last one is **after** both. The 09-11 03:05
fault is the very one quoted as evidence in patch 0014's commit message. ⇒ The guards fixed the
AI-pathfinding manifestation (crash rate 1-in-3 → 0-in-12 there) but this **fault class is still
live**. Filed `stk-highbits-pointer`; `stk-ai-crash` amended so "FIXED" is not read as "STK no longer
crashes".
★ **Instruction decoded (2026-09-12, no Pi time).** The fault is `ldrb w0, [x1, x3]` with `x3=0`,
and `x1` came from `ldp x1, x0, [x23, #40]` — the `{_M_start, _M_finish}` pair of a
`std::vector<u8>` (`GlyphLayout::draw_flags`). Two facts:
① the `ldp` address `0x0c845000` is **exactly page-aligned** — the object straddles a page boundary
and the pointer pair sits on it; ② the pair is **self-consistent**: `finish − start` is exactly the
element count (1) and **both** words carry the same bogus high half, so the bounds check *passed*.
⇒ A stray write clobbers one field and makes the size nonsense. A coherent pair means the bytes were
written as a **valid-looking vector** — the pointer **arrived** bad rather than being scribbled on.
Suspicion moves off "something corrupted STK" and onto **where those bytes came from**.
ⓘ `0x80000001` is **not** one of our constants (grepped libphoenix, kernel, port glue).
🐞★★ **A crash INSIDE the allocator's validator — found by the bench, fixed.** `guards2` T7 faulted
at `pc` = **`malloc_chunkSize` (`malloc_dl.c:136`) inlined into `malloc_chunkValid`**, instruction
`ldr x2, [x0]`, with `x0 = 0x800000000ccdc000` (bit 63 set, page-aligned). So the function whose job
is to decide whether a chunk pointer is trustworthy **dereferences it** — the one place that must not
fault. Neither STK guard applied: this is libc, not STK.
⚠ Its range test can't catch it alone (it compares against `base + heap->size`, so it only rejects
when the heap's own bounds are sound) and the `heapLo/heapHi` window test above it is **skipped while
`heapHi` is still 0**, early in a process.
✅ **Fixed** (libphoenix, committed, core build clean): reject impossible pointers *before* any
dereference — non-canonical (bits 63..47 set), NULL, or misaligned. Every corrupt pointer in this
class violates canonicality outright. A crash in the allocator becomes a rejection, which the
existing hand-out containment already handles.

★★ **Faults are STK-ONLY on the current build — so this is not systemic memory corruption.**
Across **212 logs from 09-11/12, only 5 contain any fault**: supertuxkart ×3, ntpclient ×1.
quakespasm, vkquake, quake3e, yquake2 and Xphoenix ran many times with **zero**. Free from logs.
⇒ Combined with the refuted track-data theory, that narrows it to something specific to STK — and
**STK is the only C++ game** (the others are C) and the most heavily threaded (faults in threads
60/61).
★ **Which led somewhere concrete:** libstdc++ here is built **with threads but WITHOUT TLS**
(`_GLIBCXX_HAS_GTHREADS 1`, `/* #undef _GLIBCXX_HAVE_TLS */` — verified in the installed
`c++config.h`), so its per-thread state — `__cxa_eh_globals` among it — is routed through **pthread
keys**. Every threaded C++ program leans on that path, and it had **essentially no test coverage**.
🐞 **Found and fixed a real NULL-deref there** (libphoenix, committed): `pthread_self()` returns NULL
on a thread this library did not create (e.g. `beginthread`), and both `pthread_setspecific` and
`pthread_getspecific` passed it to `pthread_ctx_get()`, whose helper does `++ctx->refcount` — a
**write to a near-zero address**, not an error. Now `EINVAL` / `NULL`. ⚠ **Not today's STK crash**
(our `far` values are large, not ~0x8) — a separate latent bug found on the way.
✅ **Added the missing TSD coverage** and ran it: **pthread_tsd 4/4, stdlib 93/0, misc 212/0, 0
faults** — per-thread isolation, a new thread starts unset while the creator's value survives,
destructor at thread exit, overwrite replaces rather than stacks.
⇒ **TSD is therefore RULED OUT as the STK culprit**: the implementation is correct, so libstdc++'s
keyed fallback (it has no TLS) is sound. The NULL-deref fix stands on its own as a latent bug.
✅ **10-trial `guards2` result:** neither STK guard fired; the only crash was the **allocator
validator** one above, now fixed. So in those ten runs the font and drive-graph bugs did not recur.
✅ **All three STK patches (0016/0017/0018) + both libc fixes now built and staged**, verified by
content. Manifest `2026-09-12-malloc-validator-and-tsd.md`.

✅ **Guarded, so STK should stop dying of it** (ports patch **0016**, committed): reading the pointer
*value* is safe even though dereferencing is not, and AArch64 user VAs are canonical (bits 63..47
zero) — which this pointer violates outright. On a bad pointer it skips **just that glyph's draw
flags**; the glyph still renders and `offset.X` still advances, so text stays placed. Deliberately
**not** `continue`, which would skip the advance and misplace every following glyph. It **reports**
(object address, both pointer words, and whether the vector header is page-aligned — it was) rather
than recovering quietly, since a silent skip would hide the only evidence the fault still happens.
⚠ **Containment, not a root cause** — the pointer arrives bad and where it comes from is still open.

★★ **And the trials immediately found a SECOND, unrelated STK crash — a fifth unguarded drive-graph
route.** `far=0x20a0ca76c` (canonical, so guard 0016 correctly did **not** fire) from
**`DriveGraph::getAngleToNext`**. Root cause is structural: `DriveGraph::getNode()`'s two `assert`s
are **compiled out under NDEBUG**, so an out-of-range index reads `m_all_nodes` **out of bounds** and
the `dynamic_cast` dereferences the result. Patches 0014/0015 guarded four *callers*; `getAngleToNext`
reaches the same chokepoint by a route they don't cover.
⇒ **Patch 0017 guards `getNode()` itself** instead of adding a fifth caller check. It **clamps** to
node 0 rather than returning NULL (every caller dereferences immediately, so NULL would only move the
crash) and **reports once** — the out-of-range index is the real bug and is still unexplained.
✅ **0017 built and staged**, both guard strings verified in the shipped binary.
**10-trial bench on 0016 alone:** the font guard **never fired** (at ~1-in-10 that is a ~35% outcome,
so it says little), and the single crash in those trials was the **drive-graph** route — exactly what
0017 targets. A 10-trial bench with **both** guards is running now.
ⓘ Built while the earlier bench still held the Pi — safe because a build writes to `.buildroot`;
only `sync-netboot-tree.sh` touches the export a running trial reads from.

★★ **Traced the bad index one level further up (patch 0018, committed, not yet built).**
`DriveNode::getSuccessor(i)` was `return m_successor_nodes[i];` — a raw vector index with **no bounds
check at all**, not even an `assert` (unlike `getNode`). `DriveGraph::determineDirection()` feeds its
result **straight back in as a node index** (`getAngleToNext(next,0)` → `getNode(next)`), so an
out-of-range successor becomes the wild node index that faulted at `far=0x20a0ca76c`.
0017 already contains the crash; 0018 checks one level up so the report names the **actual** defect
("successor i out of range on a node with n successors") instead of the laundered index arriving at
`getNode`. ⏳ Not built — the Pi is validating 0016+0017 and mixing 0018 in would muddy that result.

★★ **Predicted root cause, recorded BEFORE the test.** `computeDirectionData()` bounds its loops
correctly, but `determineDirection()` then does `next = getNode(next)->getSuccessor(0)` — assuming
**every node has ≥1 successor**. On a **dead-end** node that indexes an **empty** vector, which is
exactly the unchecked raw index 0018 guards.
⇒ **This explains the intermittency with no memory corruption at all:** the out-of-bounds read
happens *every* time, but what it returns varies with heap layout, so the resulting node index
sometimes lands inside `m_all_nodes` (silently wrong AI) and sometimes on an unmapped page (Data
Abort). That fits ~1-in-10, a different `far` each time, and why four caller-side guards reduced but
never eliminated it.
**Prediction:** 0018 should print `getSuccessor(0) out of range (0 successors)`.
⛔ **REFUTED offline the same day, no Pi time.** Hacienda's graph is a plain closed loop —
`<node-list from-quad="0" to-quad="108"/>` + `<edge-loop from="0" to="108"/>` — so **every node has
exactly one successor** and there are no dead ends. The zero-successor story cannot be what happens
here.
⇒ **That is the useful part:** if the stored graph is a clean loop over indices 0..108, a `next`
landing far outside `m_all_nodes` did **not** come from the track data or from STK's traversal logic.
It **arrived corrupt** — putting the memory-corruption thread back at the centre, alongside the font
crash where a `std::vector`'s `_M_start` was likewise bad. The two live branches for 0018's report
are now: **large `i` + sane `n`** ⇒ index arrives corrupt; **absurd `n`** ⇒ the vector header itself
is corrupt, which would tie the two STK crashes together.
⏭ Still the decisive test: dump the **whole page** at a fault — other-object bytes ⇒ page-content
bug; stale-but-plausible STK bytes ⇒ a reuse/lifetime bug.
Analysis: [`docs/misc/2026-09-12-stk-highbits-pointer-analysis.md`](../misc/2026-09-12-stk-highbits-pointer-analysis.md).
ⓘ Earlier framing of this trial: `far=0x800000010c845168`: a
valid-looking heap address (`0x0c845168`) with **garbage in its upper 32 bits** (`0x80000001`, bit 63
set — never a valid userspace pointer), read (`esr=0x92000004`) from STK code with **0 allocator
events in that run**. So it is not the allocator defect and not the fixed graph-index crash.
ⓘ Earlier: armed and verified in the STK binary; 8 trials before the fix did not trigger the detector (0 first-order, 0 bin
events, 0 faults). At the measured ~8%/run that is a **51%** outcome, so it says nothing about the
rate — the discriminator is simply waiting. It costs nothing until the fault occurs and fires in
**any** STK run from now on, including the six-app gate, so the next occurrence is decisive instead
of costing another hunt. Running tally including these: **3 event-runs in 44**.

⏭ **Aim a future hunt at chunk-header corruption, not the bins.** The dangling entry is
**second-order**: per `malloc_dl.c`'s own analysis it needs a heap released while other free chunks
are still binned, which only happens after a header **fails validation** — so the first cause is
whatever corrupts a header. The bin hand-out paths are already guarded.

⏹ **Hunting stopped.** Recent guarded builds: 1 event-run in 22 trials, down from ~1 in 3 — but I am
**not** claiming the rate dropped, because two confounds are uncontrolled (the STK guards changed how
runs end; shader-cache state varies). Twice today an apparent improvement turned out to be test
conditions. At ~1 in 22 the marginal value per Pi-hour is low and the instruments are permanent, so
the next occurrence in **any** app reports everything without another bench.

⚠ **Eleven candidates eliminated, five of them my own hypotheses** — listed in the `STK-crash` row of
`docs/KNOWN-ISSUES.md` so they are not re-walked. Three claims I made and retracted the same day: an
audio write-after-free, a shader-compilation trigger, and `rm -rf` of the cache dir disabling
caching.
⏭ Armed for the next occurrence: `hbase?=` · `freed?=` · `lheap?=` (+ `lovfl` capacity flag, after I
sized the first ring too small to discriminate) · a bounded large-bin tree audit.
Evidence trail: [`docs/misc/2026-09-10-stk-audio-heap-corruption-source-sweep.md`](../misc/2026-09-10-stk-audio-heap-corruption-source-sweep.md).

