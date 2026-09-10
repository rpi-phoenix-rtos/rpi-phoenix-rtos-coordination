# SuperTuxKart heap corruption — source sweep of the audio stack (2026-09-10)

Companion to the STK-crash row in `docs/KNOWN-ISSUES.md`. This records what a full
read of the audio stack did and did not find, so the next session does not repeat it.

## What the allocator evidence pins down

27 reports in one `stk --track=hacienda --numkarts=4 --profile-laps=2` run, all
`free-bin link is not a plausible chunk`, from `_malloc_chunkRemove()`
(`libphoenix/stdlib/malloc_dl.c`). Corrected reading of the fields:

* The trigger is the **8-byte alignment** test on `next` (`0x8003 & 7 = 3`), NOT the
  heap-range test — `0x8003` is inside `[heapLo, heapHi)`.
* `prev` is a **correct self-link** (`prev == chunk`, a single-element bin) in 26 of 27.
  Only `next` is wrong.
* `next` sits at `offsetof(chunk_t, next) == 16`, and `CHUNK_OVERHEAD` is also 16, so
  **`next` IS payload offset 0**. Exactly 8 bytes at the start of the payload were
  overwritten.
* The clobbering word is `0x8003` in 24 of 27, with outliers `0x7fa3` and `0x8043` —
  32771 / 32675 / 32835, i.e. within ±100 of 0x8000, **top six bytes zero**.
* Chunk addresses increase monotonically at an exact `0xd000` (53248) stride and are
  page-aligned. A fresh heap's first chunk is at `heap_base + 16`, so these are
  same-size chunks carved by splitting.

### Why "audio sample data written into a freed block" is the WRONG reading

An earlier note (and `KNOWN-ISSUES.md`) read the three values as 16-bit PCM near full
scale. That cannot be right as stated:

* A run of S16 or F32 samples would leave payload bytes 2–7 non-zero and would also
  destroy `prev`. `prev` is intact in 26 of 27.
* A 2-byte store into a binned chunk would leave the surviving 6 bytes of the real
  `next` (which `LIST_ADD` sets to `chunk` itself), giving `next = 0x0000ffff…8003`.

So the observed word is a **single zero-extended integer store at the block base** — a
`size_t` count, or a `{uint32_t n; uint32_t 0}` pair. Header-shaped, not stream-shaped.

### Size window, and the pool-vs-loop ambiguity

`malloc()` does `size = CEIL(max(n + 16, 40), 8)`, so chunk `0xd000` implies a request
of **n ∈ [53225, 53232]** (exact split) or **[53193, 53232]** (remainder < 40 swallowed).

`malloc_getlidx(0xd000)` = **lbin 15, spanning chunk sizes 49152–65535**. Because the
report *abandons the bin* (`lbins[idx].root = NULL`), each event leaks its chunk, which
explains the monotonic increase but **not** the exact `0xd000` stride. Two readings
survive and source cannot separate them:

* **pool** — 27 identical live objects carved contiguously;
* **loop** — one site allocating/freeing one constant size, each freed chunk quarantined
  by the abandon, the next request carved fresh at +0xd000.

## Which OpenAL

`-DUSE_MOJOAL=ON` (`sources/phoenix-rtos-ports/supertuxkart/port.def.sh`): the bundled
**MojoAL** (`stk-code-1.4/lib/mojoal/mojoal.c`). There is no openal-soft port in the tree.

Two configuration facts worth knowing:

* SDL2's audio backend here is repo-local: `sources/phoenix-rtos-ports/sdl2/overlay/src/audio/phoenix/SDL_phoenixaudio.c`,
  `/dev/audio0`, forced **S16SYS / 2ch / 44100**.
* MojoAL asks SDL for **F32SYS / 2ch / 48000 / 1024**, because STK calls
  `alcCreateContext(device, NULL)`. **48000 ≠ 44100**, so SDL's resampler is always
  engaged and every MojoAL source permanently holds a libsamplerate `src_state`.

## Ruled out, with arithmetic

| candidate | site | size | chunk | verdict |
|---|---|---|---|---|
| music PCM staging | `src/audio/music_ogg.cpp:337` (`m_buffer_size = 11025*4`) | 44100 | 44112 (0xAC50) | out |
| music AL buffer data | `mojoal.c:4478`, `size=44100`, `len_mult=2` | 88224 | 88240 | out |
| SDL AudioStream work buf | `SDL_audiocvt.c:882` (8192 + 7528 + 4464 + 32) | 20184 | 20232 | out |
| SDL device work buf | `SDL_audio.c:1517` | 8192 | 8208 | out |
| Phoenix driver mixbuf | `SDL_phoenixaudio.c:143` | 4096 | 4112 | out |
| MojoAL `SourceBlock` | `mojoal.c:3322` (64 × 224 + 8) | ~14352 | ~14376 | out |
| MojoAL `BufferBlock` | `mojoal.c:4326` (256 × 40 + 8) | 10248 | 10264 | out |
| MojoAL `ALCcontext` | `mojoal.c:1907` | ~200 | ~230 | out |
| libsamplerate state | `src_linear.c` | < 200 | — | out; SDL2's `SDL_config.h` has `#undef HAVE_LIBSAMPLERATE_H`, so SDL never calls `src_new` |
| SDL `DataQueuePacket` | `SDL_dataqueue.c:73` | ~4120 | — | out (right *shape*, wrong size) |
| capture ring | `mojoal.c:2428` | — | — | out, STK never captures |
| port patches | `supertuxkart/patches/` | — | — | only 0011 touches `src/audio/`, and it moved the 44100-byte buffer stack→heap without changing its size |

Across all of `src/audio/*.{cpp,hpp}` there are exactly **two** bulk PCM allocations:
`sfx_buffer.cpp:189` and `music_ogg.cpp:337`.

## The one surviving candidate

`stk-code-1.4/src/audio/sfx_buffer.cpp:186-189`

```cpp
long len = (long)ov_pcm_total(&oggFile, -1) * info->channels * 2;
std::unique_ptr<char []> data = std::unique_ptr<char []>(new char[len]);
```

Allocated at :189, filled by `ov_read`, consumed by `alBufferData` at :202, freed when
the `unique_ptr` leaves scope at :224 — a genuine alloc/free pair, driven in a loop over
every SFX type (`sfx_manager.cpp:588-604`). It is the only audio allocation whose size is
data-dependent and can land in the window. `len = 53232` is 13308 stereo frames =
**302.0 ms at 44.1 kHz**.

**Not confirmed, and it partly does not fit.** The `.ogg` assets are not in this repo, so
no file's `ov_pcm_total` can be checked; and 27 *different* SFX files having identical PCM
length is implausible — which is why the pool-vs-loop ambiguity above matters.

## Two real MojoAL bugs found on the way (neither is this corruption)

**(a) `mojoal.c:1212` — unbounded `alloca` in a loop.** Inside `mix_source_buffer`'s
`do { … } while (*len > 0)`:

```c
float *data_out = (float *) alloca((framesneeded - used_frame) * buffer->channels * sizeof(float));
```

4096 B (mono) or 8192 B (stereo) per iteration, never released until return, on SDL's
audio thread. **Not the corruption mechanism:** libphoenix `bad2009` / `02ab4e0`
(2026-09-07, both in this build) give aarch64 a 256 KiB default thread stack with a
one-page guard, so an overrun here **faults** rather than silently corrupting. Worth
fixing as robustness, and a plausible contributor to the *faults* in the STK-crash row.

**(b) `mojoal.c:4490` — `SDL_realloc` on an interior pointer.** `sdlcvt.buf` came from
`calloc_simd_aligned` (`mojoal.c:362-375`), which returns `base + 8 + padding` and stashes
`base` at `retval[-1]`. Reallocating that is unbounded heap corruption, and the later
`free_simd_aligned` would free a wild pointer. **Provably not taken here:** S16→F32 has
`len_mult == 2` and `SDL_ConvertAudio` yields `len_cvt == size*2` exactly, so
`sdlcvt.len_cvt < (size * sdlcvt.len_mult)` is false (same for 8-bit, `len_mult == 4`).
It fires for any format whose converted length actually shrinks.

## What closes it

libphoenix `7bc30ed` added `size`, `heap`, `pay2`, `pay3` to this report. One STK cycle
on it gives the chunk size **directly** (ending the stride inference), `size & CHUNK_CUSED`
(write-after-free vs duplicate hand-out), and whether the corruption runs past the two
link fields. A source-only search cannot resolve the size, because both surviving
candidates are data-dependent on assets absent from this repo.

---

# ★★★ UPDATE 2026-09-10 (widened report): the "corruption" is a HEAP BASE in a free bin

The widened report (libphoenix `7bc30ed`) ran on hardware and **overturns the reading above**.
This section supersedes the audio-path framing; keep the elimination table, it is still useful, but
stop looking for an audio buffer.

A representative event from `rpi4b-uart-20260910-125321-stkfix.log` (13 events in one run):

```
malloc: free-bin link is not a plausible chunk -- abandoning the bin
malloc:   chunk = 0x000000000cd72000
malloc:   next  = 0x0000000000008003
malloc:   prev  = 0x000000000cd72000
malloc:   size  = 0x000000000000d000
malloc:   heap  = 0x0000000000000000     <-- a chunk can never have this
malloc:   pay2  = 0x000000000cc71a70
malloc:   pay3  = 0x0000000000000000
```

`chunk->heap == NULL` is impossible for a real chunk: `malloc_chunkInit()` sets `->heap` on every
chunk it creates, and `malloc_chunkIsLast()`/`malloc_chunkIsFirst()` dereference it. So this address
is **not a chunk**. Reinterpreting the same bytes as a `heap_t` header followed by its first chunk
makes every single field consistent:

| offset | as `chunk_t` | value | as `heap_t` + first chunk |
|---|---|---|---|
| +0 | `size` | `0xd000` | `heap->size` — an exact page multiple (13 pages) |
| +8 | `heap` | `0` | `heap->freesz == 0`, i.e. nothing free |
| +16 | `next` | `0x8003` | first chunk's `size`: 0x8000 with `CUSED\|PUSED` set |
| +24 | `prev` | *== chunk* | first chunk's `heap`, pointing back at the heap base |
| +32 | `pay2` | pointer | that chunk's payload, or an rbnode field |

The arithmetic closes: a `0xd000` heap has `0xd000 - sizeof(heap_t)` = 53232 usable bytes, and
`32768 + 20464 = 53232`, both allocated — which is exactly why `freesz` is 0. The earlier outliers
`0x7fa3` and `0x8043` are simply other first-chunk sizes (`0x7fa0`, `0x8040`) carrying the same two
flag bits, not "PCM near full scale".

## What this explains, and what it kills

Explains, without any audio involvement:

* **page-aligned "chunks"** — heap bases come from `mmap()`, so they are page-aligned by
  construction, whereas a heap's first chunk sits at `heap_base + 16` and can never be;
* **the exact `0xd000` stride** — consecutive same-size heaps, `heapSize = CEIL(sizeof(heap_t) + n, _PAGE_SIZE)`;
* **`prev == chunk`** — not a single-element list self-link, but a valid `->heap` back-pointer;
* **`next` failing the 8-alignment test** — `0x8003 & 7 == 3` because it is a size *with flag bits*,
  which is why the range test never fired: `0x8003` is inside `[heapLo, heapHi)`.

Kills:

* the **`sfx_buffer.cpp:189`** candidate and the whole ~53232-byte search — the 53248 figure was
  the heap size, never a request size, so the `[53193, 53232]` window was an artefact;
* the **"302 ms of 16-bit stereo at 44.1 kHz"** coincidence, which was numerology on a page multiple;
* the **write-after-free by an audio owner** mechanism. Nothing was written into a freed block; a
  pointer to a heap header got into a free bin.

Still standing: `--no-sound` gives 0 events. That now reads as the audio path being what *drives the
large-allocation path* (MojoAL/Vorbis/libsamplerate do the big allocations), not as the audio path
being the writer.

## The remaining crash is the same defect, one step further along

The same run still ends in a fault, and it is now inside the allocator rather than in STK:

```
Exception #36: Data Abort (EL0)  esr=0x92000004  pc=0x91c9a8  far=0x4423400051392000
pc -> malloc_chunkSize (malloc_dl.c:101)   in "/usr/bin/supertuxkart"
```

`far` is wild, so `malloc_chunkSize()` dereferenced a non-chunk. The plausibility check catches 13
of these and abandons the bin; one gets past it and faults. Two of the four fault PCs recorded in
the STK-crash row were already inside the allocator — consistent with this being that row's actual
mechanism rather than a separate bug.

## Where to look next (not yet done)

The question is now narrow: **how does a heap base get into a free bin?** Candidates, in order:

1. `_malloc_chunkSplit()` — `sibling = chunk + size`, and if `malloc_chunkSize(chunk) == size` the
   sibling lands on the heap's end byte. Heaps here are *adjacent* (the `0xd000` stride proves
   `mmap` is handing out neighbouring regions), so a zero-size split at the last chunk would point
   the sibling straight at the next heap's base and `_malloc_chunkAdd()` it. Check
   `malloc_chunkCanSplit()` covers the equality case at the heap's last chunk.
2. `_malloc_allocLarge()`'s rbtree path — `lib_treeof(chunk_t, node, ...)` subtracts
   `offsetof(chunk_t, node) == 32`, so a tree link pointing at `heap_base + 32` yields exactly the
   heap base. `heap_base + 32` is the first chunk's payload, i.e. live user data.
3. The large-bin abandon path itself (`lbins[idx].root = NULL`) versus entries still threaded
   through `next`/`prev`.

Note that nothing wrote `next`/`prev` on this entry (`next` is the heap's own bytes, not a
self-link), which argues the entry reached the bin **without** going through `_malloc_chunkAdd`'s
`LIST_ADD` — favouring (2) or (3) over (1).

## First-cause candidates REFUTED by reading the code (2026-09-10)

The heap-release guard (`cdea6dc`) is containment: it stops a dangling bin entry from being
*created*, but the invariant it defends only breaks after some earlier problem. Three candidates for
that earlier problem are now ruled out — do not re-walk them:

1. **Runt splits leaving an unreachable chunk.** `malloc_chunkCanSplit()` requires
   `malloc_chunkSize(chunk) >= size + CHUNK_MIN_SIZE`, so a split always leaves a remainder of at
   least `CHUNK_MIN_SIZE` (40). No sub-minimum sibling is ever created, so `malloc_chunkNext()`'s
   `+ CHUNK_MIN_SIZE` slack in `malloc_chunkIsLast()` cannot skip over a real chunk.
2. **A chunk handed out while still in its bin, via the no-split path.** `_malloc_allocFrom()` is
   `if (canSplit) split(); else _malloc_chunkRemove(chunk);` — both branches unlink. (The split
   branch unlinks inside `_malloc_chunkSplit()`.)
3. **`freesz` mis-accounting on a split.** `_malloc_allocFrom()` decrements by the *post-split*
   `malloc_chunkSize(chunk)`, and the sibling stays free and stays counted. Consistent.

Also checked and NOT the source: `_malloc_chunkJoin()` validates each neighbour against the freed
chunk's own heap (`malloc_chunkValid(sibling, heap)`), so a coalesce cannot walk out of its heap into
an adjacent one — it reports `stopping coalesce` and breaks instead.

### The instrument that would settle it

If the new `heap fully free but not one chunk` diagnostic does NOT fire while corruption still
appears, the dangling-entry story is wrong and guessing further is waste. The direct instrument is a
small ring buffer of the last N `munmap`ed `(base, size)` pairs, consulted when a free-bin link is
rejected: if the rejected address falls inside a recently released heap, the stale-pointer-into-a-
reused-heap mechanism is **proven** rather than merely consistent. Cheap, and independent of whether
the guard fires.

---

# ★★★ UPDATE 2: `hbase?` PROVED the heap-base reading; the munmap mechanism is REFUTED

Run `rpi4b-uart-20260910-155535-postsd.log`, STK, 25 events, with libphoenix `3e78cbf`:

* **`hbase?=1` on 24 of 25 events.** The bin held a pointer to a **heap base**, not a corrupted
  chunk. No longer an inference from a hand decode — the allocator says so.
* **`heap = 0x4ff0` = 20464**, which is *exactly* the remainder derived by hand from the first
  event (`32768 + 20464 = 53232 = 0xd000 - sizeof(heap_t)`). Under the heap reading that field is
  `heap->freesz`, so this heap has 32768 bytes allocated and 20464 free.

That second number settles a question the first event could not, because there `freesz` read 0:
**the heap is LIVE, not fully free.** Consequences:

* The **munmap / dangling-bin-entry mechanism is refuted.** A partially allocated heap is never a
  release candidate, and the guard's own diagnostic (`heap fully free but not one chunk`) fired
  **0 times** across this run and the 3-trial bench. `cdea6dc` is harmless and worth keeping as
  hardening, but it is **not** this defect's mechanism. Retracted.
* The entry did **not** arrive through `_malloc_chunkAdd`: its `next` still reads the heap's own
  bytes (`0x8003`, the first chunk's size-with-flags) rather than the self-link `LIST_ADD` writes.

## Where the evidence now points

Large bins are an rbtree plus a same-size list. The tree is walked with
`lib_treeof(chunk_t, node, ...)`, which subtracts `offsetof(chunk_t, node) == 32`. So **a tree link
pointing at `heap_base + 32` yields exactly `heap_base`** — and `heap_base + 32` is the *payload* of
the heap's first chunk, i.e. live user data. A 32768-byte STK buffer holding pointer-like bytes is
enough for a walk that strays into it to surface arbitrary addresses.

So the question is now: **how does a large-bin tree link come to point into user data?** Checked and
NOT an obvious gap: `_malloc_chunkRemove()` does relocate the tree node when the chunk being removed
owns it but its same-size list is non-empty (`next->node = chunk->node` + `rb_transplant()` +
re-parenting both children), and it calls `lib_rbRemove()` when the list empties. The
`chunk->node.parent == &chunk->node` marker distinguishes "in the list only" from "in the tree".

Next places to look, in order:
1. `rb_transplant()` when the transplanted node is the tree **root** — does `lbins[idx].root` get
   updated, or left pointing at the old node inside a block about to be handed out?
2. The abandon path's `lbins[idx].root = NULL` versus entries still threaded through `next`/`prev`:
   the tree is dropped but the same-size lists are not, so a later `_malloc_chunkAdd` can
   `lib_rbInsert` into a tree whose root was discarded while stale list links persist.
3. `realloc()`'s `_malloc_chunkJoin(sibling)` path, which is the one caller not yet read closely.

The 1 event with `hbase?=0` is worth a separate look: it is either a genuinely corrupted chunk
header or a heap whose first chunk had already been split, and the two need different fixes.
