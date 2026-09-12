# STK corruption: there are TWO signatures, and only one of them is corruption

*2026-09-12. Companion to the `freebin-corruption` row in `docs/KNOWN-ISSUES.md`.*

## The error this file corrects

I read a chunk header full of sequential `uint16` data and concluded "an index-buffer
writer is stomping allocator metadata — that's the origin". Then I sent a subagent to
find that writer in Mesa. **The premise was wrong**, and a second subagent caught it by
pointing at the arithmetic: the two records do not describe the same event.

## Signature A — high-word-only clobber. THIS is the corruption.

A 4-byte write at an 8-aligned address **+4**. The low 32 bits of the victim field are
left intact:

```
heapsz  = 0x80000001_0000d000     real size 0xd000          <- low half INTACT
heapEnd = 0x80000001_0ccad000     = heap + heapsz
chunksz = 0x00030002_00062ff0                               <- low half INTACT
sibling = 0x00030002_0cd0b000                               <- low half INTACT
x2      = 0x80000001_09ebc080     the faulting pointer
```

`heapsz` is `it->heap->size` **read from memory**, and `it->heap == refheap` (the
trusted reference heap), so this is a live heap header. `heapEnd - heapsz` is exactly
the heap base, which proves the faulting address is that arithmetic and nothing else.

**It is a marker of the genuine events, not background noise.** Non-zero high words
appear in 3-of-9, 3-of-3 and 2-of-5 diagnostic records in the three runs that actually
faulted, and in **1 of 18471** records from a build I had broken (which produced
thousands of *spurious* rejections). It tracks real failures and ignores fake ones.
The identical value recurs across two boots and two builds ⇒ deterministic, not random.

## Signature B — dense `uint16` run. Probably NOT corruption.

Chunk `0x0ccfb000`, every field overwritten by a contiguous `0,1,2,…,22` run
(one repeat: `…16, 7, 17, 18…`), low halves destroyed:

| offset | field | u16 values |
|---|---|---|
| 0 | `size` | 0, 1, 2, 3 |
| 8 | `heap` | 4, 5, 6, 7 |
| 16 | `next` | 8, 9, 10, 11 |
| 24 | `prev` | 12, 13, 14, 15 |
| 32 | `pay2` | 16, **7**, 17, 18 |
| 40 | `pay3` | 19, 20, 21, 22 |

**That record's own probe reads `hbase?=0`** — the address is *not* a live heap base.
So the memory is legitimately an index buffer, and the allocator is reading a **stale
bin pointer** into it. Nothing wrote over allocator metadata here; the allocator walked
into someone else's live data. That is the already-known stale-entry mechanism.

## Why they cannot be the same writer

A contiguous `0,1,2,…` fill starting at the field would put `0x0003000200010000` into
the victim and **destroy the low half**. Signature A's low halves are intact, and
`0x8000`/`0x80000001` does not appear anywhere in `0..22` under any grouping. Two
different phenomena.

⇒ The Mesa index-writer hunt was aimed at signature B. For the crash it is moot.

## What the hunt did produce (worth keeping)

- **Mesa `vbo_save_api.c:649-663`** calls `u_index_generator()` with **no
  `u_trim_pipe_prim()`** first (`u_primconvert.c:129` does trim). `GL_POLYGON` /
  `GL_QUAD_STRIP` with `vertex_count == 1` computes `(1u - 2) * 3 == 0xFFFFFFFD`, and
  `if (new_count > 0)` then passes that to the generator. V3D omits QUADS/QUAD_STRIP/
  POLYGON from `prim_types`, so the conversion path is live. Real latent bug; the only
  guard on the surrounding index arithmetic is an `assert()` — **compiled out under
  NDEBUG**, the same class as the STK drive-graph bug already fixed.
- **STK `font_with_face.cpp`**: `fallback[]` is written at an all-glyph index but read
  at a compact index (newlines `continue` before the push), so for multi-line text
  `fallback[n]` is the wrong entry — and when it reads true the `sprite_id` bounds
  check is **skipped entirely**, indexing the smaller fallback bank unchecked. The
  resulting wild `ITexture*` reaches `grab()`/`drop()`, which are **4-byte
  read-modify-writes** — the right *shape* for signature A (scattered 4-byte writes),
  though a `++` on a zeroed high half yields `1`, not `0x80000001`, so the value is
  not yet explained.
- **STK `font_manager.cpp:671`**: `drawText`/`getDimension` hold a **reference into**
  `m_cached_gls` across the whole of `render()`, and `clearCachedLayouts()` can free it
  mid-pass. `initGlyphLayouts` copies and is safe; these two do not. Structurally
  unsound, not proven reachable. Cheap decisive test: take a copy and re-measure the rate.

## Status

Contained, not root-caused. `malloc_heapSizeValid()` (libphoenix `11286c0`) bounds
`heap->size` from above so the walk stops instead of dereferencing; the writer of
signature A is still open.
