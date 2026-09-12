# W37 settled sections, moved out of the weekly log (2026-09-12)

Demo-repeatability gates, the V3D clock-race writeup, the SuperTuxKart AI crash fix, and the
allocator free-bin investigation as it stood before the 4-byte-write shape was found. Kept verbatim;
the live summary is in the weekly log.

## 2b. ✅ DEMO REPEATABILITY ON THE DELIVERED CODE — 3 full gates, 18 cycles

**17 of 18 app cycles clean**, all six apps rendering every time (content-graded from HDMI, not
logs). The one blemish was **not the app**: `/bin/ntpclient` entered a NULL-write crash loop and
flooded the UART with **2070 Data Aborts**, which cost that Quake II cycle its flipstat output while
it was still on screen. New `ntpclient-null-loop` row in `docs/KNOWN-ISSUES.md`; ~1 in 60 boots, no
rendering impact observed, not root-caused.
★ Worth the gates: they caught a defect in a *background* process that every app-focused test had
missed — and the lesson is to attribute an exception storm via the dump's process name before
blaming whatever was in the foreground.

## 2b2. ✅ Full 6-app gate detail — 6/6, zero faults

The gap this closes: every check since the STK guards landed had been piecemeal (STK benches, X11
alone, the libc suite). This is the **complete showcase sequence the owner would drive**, in one
pass, on the same ports SHA the image carries (`13d4a89`).

**6/6 `rc=0`, prompt reached, launched — and 0 exceptions AND 0 corruption events on every app**
(post-banner counted, so no stale-buffer artifact). Content-graded from the HDMI frames:
X desktop 89.4% non-black · QuakeSpasm 36.4% · Quake III 29.2% (11537 colours) ·
Quake II **10591 frames @ 38.8 fps** (its 14.8% grade is the small viewport, 6178 colours) ·
vkQuake 91.4% · SuperTuxKart **99.9%, 47152 colours**.

### 2c. The intermittent app-start stall — a V3D bug found and FIXED; the original one REOPENED

How the hunt got here, in four lines: a **launch** is a far cheaper unit of exposure than a **boot**,
so `/bin/spawn-storm` (tests **`8d00cda`**) storms one program per boot. ~4200 launches of
`bash`/`python3`/`cxxprobe` (C and C++, sequential and 4-way concurrent) stayed **clean**, which said
the fault is **binary-specific**; storming the real `/usr/bin/quakespasm -loadbench` then reproduced
it, and the committed startup trace — deployed in a port binary for the first time — named the phase.
ⓘ Byproducts kept: `vfork`+`execv` is solid over ~4200 cycles (process spawn is not a demo risk), and
**0 `object EOF at`** across all of them, so kernel `6cd3adec`'s premature-EOF path does not fire even
under heavy demand-paging load. Detail:
[`docs/misc/2026-09-11-premain-hang-hunt.md`](../misc/2026-09-11-premain-hang-hunt.md).


⚠ **Correction (2026-09-11): I conflated two failures and marked the wrong one fixed.** The V3D
clock race below is real, fixed and shipped. But the **original** report was a launch that printed
**nothing at all** before the prompt stopped coming — and the V3D stall prints `main() entered` plus
~15 further lines *before* it hangs. So the V3D fix does **not** explain the original, and
`premain-hang` is **reopened** in `docs/KNOWN-ISSUES.md`. The distinguishing test is now cheap: storm
`-loadbench` and check whether a stalled launch printed `main() entered` — **absent ⇒ a true
pre-`main` hang**, present ⇒ the V3D path.

### ✅ The intermittent GPU-app start stall was a **V3D clock race** — FIXED and SHIPPED

Power-on toggled the V3D clock off/on around the reset deassert through the **racy direct mailbox
FIFO with the result discarded**; losing that race left the clock off, and the next V3D MMIO read
**never completes** (SError masked, TD-10) — a silent, unkillable hang at GPU-app startup. Fixed by
enabling through the serialized `/dev/vcmbox` with read-back, and by checking `v3d_phoenix_powerOn()`'s
return at **both** the init and runtime-reset call sites. The same pattern existed a second time in
the standalone `/sbin/rpi4-v3d` daemon and was fixed there too.
**Measured proof it was real:** the cold-state probe's bogus `MBOX_FAIL` readings went **~9% → 0%**.
Shipped in `rpi4b-sd-2part-gated-1f493117.img` (§2). Detail:
[`docs/done/2026-09-12-v3d-clock-race-shipped.md`](../done/2026-09-12-v3d-clock-race-shipped.md).
## 2d. ★ DEMO REPEATABILITY, and SuperTuxKart's crash is FIXED

**3 consecutive 6-app gates, 18 cycles: 17 clean.** Zero allocator corruption on every app, and the
armed instruments produced no output at all, so nothing was contained-but-hidden. No launch hit the
pre-`main` hang.

✅ **The one failure was SuperTuxKart's own AI, and it is now fixed.** Root cause in STK's code:
`DriveGraph::getNode()` bounds-checks only with an `assert()` **compiled out under `NDEBUG`**, so a
`-1` sector indexes ~32 GiB past `m_all_nodes` and `dynamic_cast` dereferences the garbage vptr.
**Four routes** reached it (two unsigned-conversion boundaries, two direct array indexes with
`m_track_node`); all four guarded by port patches **0014 + 0015** (phoenix-rtos-ports `13d4a89`).

**Verified: 0 crashes in 12 trials at `--numkarts=4`** against a **1-in-3** baseline (~0.8% by luck),
corroborated beforehand by `--numkarts=1` (which removes the AI) giving 0 in 7.
★ **So your kart-count trade-off is gone** — four karts *and* stability, and `--no-sound` is moot for
this crash. STK is GPL-3, so the fix lives in the port patches, never a core repo.
Full analysis: [`docs/misc/2026-09-11-stk-ai-pathfinding-crash-rootcause.md`](../misc/2026-09-11-stk-ai-pathfinding-crash-rootcause.md).

★ **Before presenting or recording: run each GPU app once to warm up** — a cold first run costs ~35 s
(55.5 s vs 20.0 s per profile lap, measured by the sync script), and don't rebuild GPU archives
right beforehand.


## 5. Allocator free-bin corruption — contained; first cause still open

A free bin ends up holding a pointer to memory that is not what the bin thinks. **Contained** on both
hand-out paths, so it no longer ends runs (a 22-event run finished 0 faults / 1301 frames). Rate ~3
event-runs in 44, STK only.

**What this week established** (all from existing logs, no Pi time):
- **Not** STK's out-of-bounds index — the rate was unchanged across that fix (4.8% → 8.3%).
- The dangling bin entry is **second-order**: it needs a heap released while other free chunks are
  still binned, which only happens after a chunk header **fails validation** first.
- The first-order event (`corrupt … neighbour`) *is* logged: **11/11 derived siblings page-aligned**
  while their chunks were only 16-byte aligned (p≈1e-27 by chance) ⇒ the walk reaches a heap **edge**
  by construction, **not** a userspace buffer overflow (the theory written in the source).
- ⇒ Fixed a real gap: `malloc_chunkValid()` never checked `chunk->heap == heap`, so
  `_malloc_chunkJoin()` now computes its boundary from the **trusted reference heap**.
- 🐞 And the validator itself could fault: it dereferences the pointer it is validating. It now
  rejects, **before any dereference**, (a) non-canonical / NULL / misaligned chunks, and (b) chunks
  inside an **already-released heap**.
  ⓘ (b) came from a second crash at the *same instruction* with a different cause: `chunk=0x0cdfa000`
  was **canonical and page-aligned** and passed the range test — because that test is measured against
  the **stale heap's own header**, which can still be readable after the heap's later pages are gone.
  ⚠ Deliberately a **"known bad"** test (`malloc_wasReleased`), which can only miss, never
  false-reject. The `live[]` ring cannot serve the opposite test: it is a 256-entry diagnostic with a
  documented overflow counter, so once it wraps a "not live" answer would reject **valid** heaps and
  stop coalescing entirely.

📊 **Control arm measured (12 STK trials, build with the STK guards + canonical-pointer fix but
WITHOUT the released-heap guard): 1 crashed run in 12 (8.3%)** — matching the historical ~1-in-10, and
that crash was exactly the released-heap case (`chunk=0x0cdfa000`) the new guard now rejects. A
12-trial post-fix arm is running against the same track and settings, so the two are comparable.
**Post-fix arm: 12 trials, 0 crashes — but also 0 abandons and `leaked=0`.**
⚠ **That `leaked=0` is the important number: the new release guard NEVER FIRED**, so the fix was not
exercised and its 0 crashes do **not** validate it. The triggering condition simply did not occur —
abandons are per-run rare (the control's 17 were all inside its single crashed run). So the two arms
differ by whether abandons happened at all, not by the fix. **No evidence either way yet.**
✅ What *is* verified: the fix breaks nothing — **stdlib 93/0, misc 212/0, pthread_tsd 4/0, 0 faults**
— so it is pushed (libphoenix, manifest `2026-09-12-malloc-release-guard.md`).
⏭ To actually test it, a run must produce an abandon; `heap release ABANDONED` in any future log is
the signal that it did its job.

★★★ **Likely ROOT CAUSE found (committed, NOT pushed — awaiting HW regression).**
`_malloc_chunkRemove()` **abandons** the bin when a link fails validation: it reports and returns
*without unlinking*. It was `void`, so the **heap-release path could not tell** — and it `munmap`'d
the heap anyway. `mmap` then reuses that region, so the still-linked entry later reads as a perfectly
sane chunk header belonging to the **next** heap. That is exactly the shape this file's own analysis
describes, and it is what faulted inside `malloc_chunkValid()` on `chunk=0x0cdfa000`.
⇒ `_malloc_chunkRemove()` now returns 1 on a real removal and 0 on abandon (including the
stale-large-bin-node path, where the chunk leaves the list but the tree still owns its node — also not
a clean removal). The release path checks it and **leaks the heap instead of unmapping**, and says so.
Leaking one heap beats a dangling bin entry, which is unbounded corruption — the same trade the
existing single-chunk check already makes. Core build clean.

📊 **And the logged abandons CONFIRM that mechanism — 208 of them, free from existing logs.**
Every abandon prints the wild link, and the shape is uniform: `chunk` **page-aligned at the exact
`0xd000` heap stride** (consecutive heap bases), `prev` == the chunk itself, and `next` = **`0x8003`**
— not a pointer at all but a chunk **size** `0x8000` with both used-flags set.
⇒ That is *verbatim* what `malloc_dl.c`'s own comment predicts for a stale entry whose memory `mmap`
recycled as a new heap: "the first chunk's size where `next` belongs". And no code inserts a heap base
into a bin — all five conversion sites correctly use `heap->space`. So the entries are **stale
pointers into recycled regions**, which is exactly the step the held fix prevents.

⏭ **Open:** what makes the *first* link fail validation (the abandon's trigger). The fix stops an
abandon escalating into unbounded corruption; it does not explain the first one. A discriminator is armed
(`refheap`/`chunksz`/`heapsz`/`heapEnd` on the next event).
Detail: [`docs/done/2026-09-12-allocator-corruption-investigation.md`](../done/2026-09-12-allocator-corruption-investigation.md).
