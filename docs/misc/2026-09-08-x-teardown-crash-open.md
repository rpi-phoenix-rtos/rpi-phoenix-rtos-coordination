# X desktop-exit crash — still OPEN, but the fault is finally DECODED (2026-09-08)

Read this before attempting a fifth fix. **Four have now been tried on hardware; all four
failed.** Today's gain is not a fix: it is that the crash dump is decoded *correctly*, which
retires two wrong readings that had been steering every previous attempt, and narrows the
surviving hypothesis to one specific object.

Reproduce: `startx_gpu --quit-after 90 deskapps` (that flag exists only for this — the image
ships no `ps`/`pkill` and `xlaunch` blocks in `waitpid`). Fires every run. Impact is
shutdown-only: psh resumes, the box stays up, and none of the 21 gated boots are affected.

## The one-line statement of the bug

`DamageUnregister()` dereferences `pDamage->pDrawable->pScreen` where **`pDamage->pDrawable` is
stale** — it points at memory that is not a live drawable. Everything below is
register/disassembly evidence, not inference.

## Decoding the fault (this is the part to trust)

Fault, stable across every run:

| field | run 1 (`wmquit2`) | run 2 (`stipplefix2`) |
|---|---|---|
| `pc` = `lr` | `0x51d444` | `0x51d444` |
| `far` = `x0` | `0x60` | `0x10000005f` |
| `x1` | `0` | `0` |
| `esr` | `0x92000007` | `0x92000005` |

`0x51d444` is `dixGetPrivate+0x68` in **damage.c's** copy of that `static inline`. The binary
carries ~110 per-TU copies of it; *which* copy the `pc` lands in is what identifies the
translation unit (`nm -S` bound: the copy at `0x51d3dc`, size `0x74`, sits between
`dixPrivateKeyRegistered` and `getDrawableDamageRef`). Disassembled:

```
0x51d440:  bl   51d360 <dixGetPrivateAddr>
0x51d444:  ldr  x0, [x0]        <-- faults
```

so **`far`/`x0` is the *return value* of `dixGetPrivateAddr`, not an argument.** That callee
ends:

```
ldr x0,[sp,#24]   ; privates  (= &pScreen->devPrivates)
ldr x1,[x0]       ; x1 = *privates = pScreen->devPrivates
ldr x0,[sp,#16]   ; key
ldr w0,[x0]       ; key->offset
add x0, x1, x0    ; return devPrivates + offset
```

Therefore, with `damageScrPrivateKeyRec.offset == 0x60`:

* `x1 = 0` is `pScreen->devPrivates`.
* run 1: `devPrivates == 0`; run 2: `devPrivates == 0xFFFFFFFF`. **Both are garbage** — so
  `pScreen` is not a real screen, which means the `pDrawable` it came from is not a real
  drawable.

### Two readings this retires

1. ⚠️ **`x1 = 0` is NOT "the private key is NULL".** The earlier version of this file called
   that "the strongest untouched clue"; it is a register left over from `dixGetPrivateAddr`.
   The key was always fine: the stack slot `[sp+16]` holds `0x1b83450`, which the symbol table
   resolves to `damageScrPrivateKeyRec` itself, and both asserts (`key->initialized`,
   `key->size == 0`) passed before the fault. A NULL key would have faulted earlier and
   elsewhere — `dixLookupPrivate` reads `key->size` *before* calling `dixGetPrivate`.
2. **`DamageUnregister (+0x524228)` was not a bogus symbolication.** `0x524228` is an *address*
   inside `DamageUnregister` (`0x5241ec`, size `0xf8`), mis-typeset as an offset. `0x524224` is
   literally `bl dixLookupPrivate`, so `0x524228` is its return address. The symbolication was
   sound throughout.

### Call chain

From the kernel's user-stack window `fp`/`lr` pairs, every address landing **inside a sized
symbol** and cross-checked against the disassembly:

```
doFreeResource            0x42dba4      (dix/resource.c, from FreeAllResources)
 FreeGC                   0x41a1b4
  glamor_destroy_gc       0x5b2a0c
   glamor_invalidate_stipple 0x5b2624
    DamageUnregister      0x524228      (damage.c:1796, damageScrPriv(pScreen))
     dixLookupPrivate     0x51d50c
      dixGetPrivate       0x51d444      <-- ldr x0,[x0]
```

Note this is inside **`FreeAllResources()`** (`dix/main.c:295`), which runs *well before* the
screen-teardown loop at `main.c:320` that frees `pScreen->devPrivates`. So a legitimately-freed
screen private was never a possible explanation.

### Objects recovered from the frames

`DamageUnregister` keeps `pDamage` at `sp+24`, `pScreen` at `sp+56`, `pDrawable` at `sp+64`, and
its `x29 == sp`:

| local | run 1 | run 2 | verdict |
|---|---|---|---|
| `pDamage` | `0x2a59d28` | `0x2a50bd0` | heap, plausible |
| `pDrawable` | `0x3ad9c8` | `0x2438e78` | **not a live drawable** (see below) |
| `pScreen` | `0x3ad9d8` | `0x2438d88` | garbage `devPrivates` |

The ELF loads `0x400000–0x1baac80` and the heap is above it. In run 1, `pDrawable = 0x3ad9c8`
is **below the image and outside the heap**, with a self-pointer at `+16` — allocator/list
bookkeeping, not a `DrawableRec`. In run 2 it is in the heap but only `0xF0` below `pScreen`,
whereas a `ScreenRec` is `0x3d8+` bytes — so the two allegedly-distinct objects overlap, which
they cannot. Both runs say the same thing: **`pDamage->pDrawable` is stale.**

Corroboration: in both runs the stack just below `sp` still holds dead
`lib_rbInsert` / `lib_rbRemove` / `_malloc_chunkRemove` frames — a `free()` had run on this
stack moments earlier.

## Attempt 4 (REFUTED): glamor re-registering a live damage

Hypothesis: `glamor_track_stipple()` is called from `glamor_validate_gc()` on **every**
`GCStipple` change while the `DamagePtr` is created once and cached in `gc_priv`, so from the
second stipple change onward it calls `DamageRegister()` on an already-registered damage.
`damageInsertDamage()` prepends unconditionally (its `"Damage already on list"` check is behind
`DAMAGE_VALIDATE_ENABLE`, off in normal builds), which self-links the node
(`pNext == pDamage`); `damageRemoveDamage()` then writes `*pPrev = pDamage->pNext` — `pDamage`
again — leaving the list head referencing the node after `DamageDestroy()` frees it.

That is a genuine latent defect in glamor and the mechanism is real, but **it is not this
crash.** Tried: register only when `gc_priv` says the damage is not already on that drawable
(tracked in a new `stipple_damage_drawable` field so no `DamageRec` internals are touched —
`struct _damage` is opaque to glamor and there is no `DamageDrawable()` accessor in this tree),
plus the `pDrawable != NULL` guard before `DamageUnregister` that `DamageDestroy` already makes.

**Result: still crashed, and a bounded `ErrorF` on the suppressed-re-register branch never
printed once.** The double-register path is simply not taken in this workload, so the guard did
nothing. The `far` value moved (`0x60` → `0x10000005f`) only because the extra struct field
changed the heap layout, i.e. it repainted the garbage. Reverted in full; `libglamor.a` and the
staged daemon are byte-size-identical to the pre-attempt build again.

## Why all four attempts failed

Attempts 1–3 all targeted the **glamor screen private allocation**; attempt 4 targeted the
**damage list bookkeeping**. The broken lifetime is the **damage's drawable**.

1. Defer `free(pScrPriv)` past `CloseScreen` — still crashed; symptom moved to a jump through a
   stale function pointer.
2. Defer + clear the slot + guard `DamageUnregister` on NULL — moved the identical fault to
   `compDestroyWindow`. A NULL guard cannot help: `pDrawable` is non-NULL *garbage*.
3. Never free the screen private at all — **fault unchanged**, correctly refuting the
   use-after-free-of-a-private model.
4. Above. Guard never fired.

1–3 were reverted in `ports 21bd0ec`; 4 in this session.

## The surviving hypothesis, and the next step

With attempt 4's guard in place `DamageUnregister` was **still reached**, which means glamor's
own bookkeeping said "registered" while the drawable underneath was already gone. So: **the
pixmap glamor registered the stipple damage on is freed without the damage being unregistered
or destroyed.**

One concrete, unexamined mechanism for exactly that was found while reading the source and is
where the next attempt should start:

* `glamor_destroy_pixmap()` (`glamor/glamor.c:266`) calls **`fbDestroyPixmap()` directly**, not
  `(*pScreen->DestroyPixmap)()`. It therefore **bypasses damage.c's `damageDestroyPixmap`
  wrapper**, which is the only thing that walks a dying pixmap's damage list and destroys the
  entries. Any pixmap freed through that path leaves its registered damages dangling.
* Complication to check first: the damage is registered on `gc->stipple` (the *client's*
  bitmap, `glamor_core.c:186`) while `glamor_destroy_pixmap()` is called on `gc_priv->stipple`
  (glamor's converted copy, created in `glamor_transform.c:244`). Those are different pixmaps,
  so the bypass only explains the crash if the two coincide on some path, or if the client
  bitmap dies through another non-wrapper route.
* `damageDestroyPixmap` also skips cleanup entirely unless `pPixmap->refcnt == 1`, which is
  worth instrumenting.

**Do not patch on a hypothesis again.** The cheap decisive experiment is one diagnostic build
that prints, in `glamor_track_stipple` at register time and in `glamor_invalidate_stipple`
before the unregister: the `DamagePtr`, the drawable it is being registered on / was registered
on, that drawable's `type` and `refcnt`, and `gc->stipple` vs `gc_priv->stipple`. One Pi cycle
then says which pixmap died and by what route.

## Harness traps hit this session — read these before the next cycle

* **`test-cycle-psh-interact.sh` re-syncs `/bin` from `.buildroot/_fs/...` at cycle start**, so
  a daemon staged only into `/srv/phoenix-rpi4-nfs-gcc16/bin/` is **overwritten before boot**.
  The first cycle silently ran the *old* binary and its "same crash" result was worthless.
  Stage into `.buildroot/_fs/aarch64a72-generic-rpi4b/root/bin/` (the sync source) as well, and
  verify a marker string in the staged file *after* the cycle, not just before it.
* **`build-xserver-core.sh` early-exits at line ~101** when the core archives exist, and the
  glamor patch-application block sits *after* that return — so a patch added there is silently
  skipped on any subsequent run. If a durable glamor patch is ever added, move the patch block
  above the `all_present` early-exit.
* **`make -C glamor` alone can fail** with `fatal error: epoxy/gl.h`: `GLAMOR_CFLAGS` is empty
  in `glamor/Makefile` (configure did not bake it in). Pass it explicitly:
  `make -C glamor GLAMOR_CFLAGS="-I<repo>/tools/x11-port/glamor-shim -I<repo>/external/mesa/include"`.
  The top-level `make` does not descend into `glamor/` at all.

## Method notes worth keeping

* A register in a crash dump is only an argument **at function entry**. At `+0x68` into a
  callee, decode the disassembly before naming what a register holds. Two of this bug's four
  failed attempts trace back to skipping that step.
* When a binary carries many per-TU copies of a `static inline`, *which copy* the `pc` lands in
  identifies the translation unit. `nm -S` with a symbol-size bound is reliable;
  nearest-preceding-symbol without a size check is not.
* A fix whose diagnostic never prints has not been tested, however clean the run looks. Here the
  crash persisted so it was obvious — but had it disappeared for an unrelated reason, the
  never-firing guard would have been credited with it.
