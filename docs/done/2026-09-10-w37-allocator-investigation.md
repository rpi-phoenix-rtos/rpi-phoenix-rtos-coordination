# W37 — the allocator bin-corruption investigation (2026-09-10)

Carved out of the weekly log. The durable summary now lives in the `STK-crash` row of
`docs/KNOWN-ISSUES.md`; the evidence trail is in
`docs/misc/2026-09-10-stk-audio-heap-corruption-source-sweep.md`.

### 3f. The allocator defect: PROVEN shape, contained, root cause still open

**Proven.** `hbase?=1` on 24/25 then 34/34 events: the free bin holds a pointer to a **heap base**,
not a corrupted chunk. Confirmed independently by the `heap` field reading `0x4ff0` = 20464, exactly
the free remainder derived by hand (`32768 + 20464 = 53232 = 0xd000 − 16`) — so that field is
`freesz` and **the heap is live**, not released.

**Contained, and this already matters for the demo:** the latest run had **34 events, 0 faults, 1306
frames**. This defect used to end in a Data Abort inside `malloc_chunkSize()`. Validation now sits on
both bin hand-out paths — `0eab703` (large/rbtree) and **`9aaacdf`** (small/`sbins[]`).

⚠ **Four hypotheses of mine, all refuted by their own instruments.** Worth stating plainly so nobody
rebuilds them: audio write-after-free (killed by the field decode) · heap release with multiple free
chunks (`not one chunk` = 0 over 7 runs) · stale `rb_transplant` parent (`stale large-bin node` = 0
over 3 runs) · "only the rbtree lookup can produce a heap base" (`lookupbad` = 0 while 34 events
fired). The guards `cdea6dc` and `ccc0bc1` protect paths never observed to be taken — hardening, not
fixes.

★ **The lesson from the fourth:** I ruled out the small-bin path because the reported size `0xd000`
classifies as large. But **that size is read FROM the bad pointer** — a heap base reports its own
`heap->size` as a chunk size and is then printed down the large-bin branch. Size is an *output* of
the corruption, never evidence of provenance.

⚠ **My small-bin check had a false positive and I caused it.** `malloc_chunkInWindow()` required
`p + sizeof(chunk_t)` to fit under `heapHi`, which rejects legitimate chunks near a heap's end —
`sizeof(chunk_t)` includes the rbnode only *large* chunks use. `malloc_linkPlausible()` carries a
comment about exactly this trap, from an earlier occurrence that leaked live memory, and I
reintroduced it two functions away. Cost on hardware: **~390 rejections per run in ALL FOUR trials**,
including previously clean ones, each dropping a healthy bin — and it masked the real detector, so
that bench says nothing about the defect and is being re-run. Fixed range-only in **`46f456a`**.
★ A hardening check that rejects valid input is worse than no check: it turns a rare fault into
constant quiet damage.

★ **New evidence unifies the reports — it is recycled memory, not two defects.** One event's fields
decoded as a pristine heap header (`hbase?=1`); the next, at the same stride, looked like nonsense
until read as 16-bit halfwords, where it is **`0,1,2,3,…,15` — a live GPU index array**. `mmap`
reuses addresses, so a bin entry left pointing at released memory reads as a plausible chunk when
the region has become a new heap and as garbage when it has become somebody's index buffer. That is
why the surfaced values were never stable. libphoenix **`977973a`** now keeps the last 8 released
`(base, size)` pairs and prints **`freed?=`**, which *proves* a stale pointer rather than inferring
one. ⚠ This **reopens** the release mechanism I had recorded as refuted: that refutation rested on
`not one chunk` never firing, which only rules out the multi-chunk route, not the destination.

✅ **Checked and cleared:** the allocator's locking is sound — `_malloc_init()` creates the mutex at
startup (not lazily), and every public entry point, `calloc` included, is serialised.
⚠ **A fast reproducer failed to reproduce.** New test `stdlib_alloc/malloc_heap_recycle_churn`
(phoenix-rtos-tests **`c91eede`**) drives the exact field sizes — 53200 (own heap), 32752 (the 32768
first chunk), 200 (small bins) — with payload verification, and **passes on hardware (26 Tests, 0
Failures)**. So single-threaded churn is not enough; the trigger needs something it lacks, with
STK's extra threads and an app-side use-after-free in the GPU path as the two live candidates. The
test stays as a regression guard.

⏭ Open: catch one occurrence with `freed?=` in place. `freed?=1` ⇒ the allocator released a heap
with an entry still pointing into it. `freed?=0` with app data in the payload ⇒ an app-side
use-after-free. 6-trial hunt running (rate is ~1 in 4). Next step is the audit on the small
path (the tree audit `1fbb6f7` never fired, consistent with the rbtree being uninvolved).
Full evidence: [`docs/misc/2026-09-10-stk-audio-heap-corruption-source-sweep.md`](../misc/2026-09-10-stk-audio-heap-corruption-source-sweep.md).

✅ **Netboot proven intact after the `--variant sd` build** (banner, psh prompt, STK in-race).

