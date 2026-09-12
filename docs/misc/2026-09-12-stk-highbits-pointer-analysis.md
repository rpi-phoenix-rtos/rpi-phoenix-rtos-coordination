# `stk-highbits-pointer`: the faulting instruction, decoded

*2026-09-12. No Pi time — disassembly + register dump + existing logs.*

## What faults

```
4b3640:  ldp   x1, x0, [x23, #40]     ; x1 = _M_start, x0 = _M_finish  (a std::vector)
4b3644:  add   w3, w20, #0x1
4b3650:  sub   x0, x0, x1             ; size = finish - start
4b3654:  cmp   x3, x0
4b3658:  b.cs  <exit loop>            ; bounds check PASSES
4b365c:  ldrb  w0, [x1, x3]           ; <-- Data Abort
```

`FontWithFace::render(std::vector<GlyphLayout> const&, …)`, iterating a `std::vector<u8>` —
`GlyphLayout::draw_flags`. At the fault `x3 = 0`, so `far == x1`.

## Two facts from the registers

| | value |
|---|---|
| `x23 + 40` (the `ldp` address) | `0x0c845000` — **exactly page-aligned** |
| `_M_start` | `0x800000010c845168` |
| `_M_finish` | `0x800000010c845169` (= start + size, size 1) |
| shared high word | `0x80000001` in **both** |

1. **The object straddles a page boundary** and the vector's pointer pair sits exactly on it: the
   `GlyphLayout` begins at `0x0c844fd8`, so its first 40 bytes are on one page and `draw_flags`
   begins on the next.
2. **The pair is self-consistent.** `finish - start` is exactly the element count, and both words
   carry the same bogus high half.

## Why fact 2 matters

A stray write clobbers **one** 8-byte field and leaves `finish - start` nonsensical — the bounds
check would then reject, or the size would be absurd. Here the check **passed** with size 1.

So these 16 bytes were written as a **coherent vector**: the bad pointer was not scribbled over
after the fact, it was already bad when the pair was stored — or the 16 bytes are not this object's
at all.

⇒ Suspicion moves off "something corrupted STK's memory" and onto **where those bytes came from**.

## The unifying suspicion (hypothesis, not established)

This project has now collected several independent faults that all land on **page boundaries**:

- the allocator's corrupt-neighbour walks — **11/11** derived siblings page-aligned
  (`project_allocator_double_free`);
- `atexit-null-head` — a statically initialised `.data` word reading back NULL, narrowed to
  **load time**;
- and now this: an object's post-page-boundary half holding a coherent but wrong pointer pair.

A single explanation would be **a page sometimes holding contents that are not the ones that belong
to it**. That is consistent with all three, but it is **not proven** by any of them, and each has a
local explanation too. Recorded so the coincidence is not rediscovered a fourth time.

## ⏭ Next

- `0x80000001` is **not** one of our constants (grepped libphoenix, kernel headers, port glue), so it
  is not an error code or handle leaking into a pointer.
- The cheap decisive test is to dump the **whole page** around a fault, not just the registers: if
  the bytes after the boundary belong to some other object, the page-content theory is confirmed;
  if they are plausible-but-stale STK data, it is a reuse/lifetime bug instead.
- Rate is ~**1 in 10** recent STK runs, so a repro costs ~10 cycles.
