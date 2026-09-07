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

In every run the word at `pDrawable + 16` — i.e. `pDrawable->pScreen` — reads back as
`pDrawable + 16` **itself**: a self-pointer, which is allocator/list bookkeeping, not a
`ScreenRec` address. That is the reliable tell that the drawable has been freed and its chunk
recycled.

⚠️ An earlier version of this file also argued that `pDrawable = 0x3ad9c8` "cannot be a live
object because it is below the image (`0x400000-0x1baac80`) and outside the heap". **That
argument was wrong** — the later instrumented runs show live GCs and pixmaps at `0x3adc38`,
`0x3b4938`, `0x3b4860`, so that low region is a perfectly valid allocation arena in this
process. The conclusion (a freed drawable) survives, but it rests on the self-pointer and on the
`ENTER` measurement below, not on address ranges.

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

## MEASURED (2026-09-08, three instrumented Pi cycles): the drawable dies with the damage still attached

Diagnostics were added to `glamor_track_stipple` (REG), `glamor_invalidate_stipple` (UNREG) and
`damageDestroyPixmap` (ENTER, keyed on a `phx_watch_pixmap` global that glamor sets to the exact
pixmap it registered on), then reverted. All three runs agree.

Representative run (`dmgwatch`):

```
glamor/phx: REG   dmg=0x2a64b50 gc=0x3b4938 on stipple=0x3b4860 refcnt=2 type=1 (#1)
glamor/phx: UNREG dmg=0x2a64b50 gc=0x3b4938 gc->stipple=0x3b4860 priv->stipple=0x2a50860 (#1)
```

and from the crash's stack window in the two runs that reached it, `pDamage` / `pDrawable` are
**exactly the registered pair** (`0x2a61d28` / `0x3adb38`; `0x2a40d28` / `0x2437f18`).

What that establishes:

1. **The damage is registered exactly once** (`REG` prints `#1` and never again), on `gc->stipple`,
   a `DRAWABLE_PIXMAP` with `refcnt = 2`. Confirms attempt 4's refutation independently.
2. **The damage object is intact and still correctly registered at crash time** — `pDamage` is the
   same pointer, and its `pDrawable` still points at the pixmap it was registered on. Nothing
   corrupted the damage.
3. **That pixmap has been freed**: its memory now reads a self-pointer at `+16`, so
   `pDrawable->pScreen` yields allocator bookkeeping whose `devPrivates` is garbage (`0` /
   `0xFFFFFFFF` across runs) — the fault.
4. **`gc->stipple` != `gc_priv->stipple`** (`0x3b4860` vs `0x2a50860`, measured). So
   `glamor_destroy_pixmap()`'s `fbDestroyPixmap` wrapper-bypass runs on a *different* pixmap and is
   **ruled out by measurement**, not just by reading the source.
5. **`damageDestroyPixmap` is never entered for that pixmap.** The `ENTER` probe is
   unconditional on the watched pointer and prints on entry; it never fired. In the `dmgwatch`
   run `UNREG` — which happens *later* than `FreeGC`'s stipple release — did flush, so the
   missing `ENTER` is a real absence, not output lost to the crash.

`FreeGC` (`dix/gc.c:780-783`) releases the stipple **before** calling the DDX `DestroyGC`:

```c
if (pGC->stipple)
    (*pGC->pScreen->DestroyPixmap) (pGC->stipple);   /* 781 */
(*pGC->funcs->DestroyGC) (pGC);                       /* 783 -> glamor_destroy_gc */
```

Line 781 therefore ran, and `damageDestroyPixmap` did not. **Conclusion: damage.c's
`damageDestroyPixmap` is not in this screen's `DestroyPixmap` chain**, so *no* pixmap free ever
cleans up registered damages. glamor's stipple damage outlives its drawable, and `FreeGC` then
drives an unregister against freed memory.

### The one remaining question, and the probe for it

Why is the wrapper missing? `DamageSetup()` (`damage.c:1674`) does
`wrap(pScrPriv, pScreen, DestroyPixmap, damageDestroyPixmap)`, and it clearly ran — `pScrPriv` is
valid (`DamageRegister` dereferences `pScrPriv->funcs.Register` successfully). Its only callers in
this tree are `mi/misprite.c:283`, `damageext/damageext.c:734` and `miext/shadow/shadow.c:122`.
So either the wrap never happened, or **something later overwrote `pScreen->DestroyPixmap`** —
a wrapper-chain LIFO violation, which `damageDestroyPixmap` itself invites by unwrapping and
re-wrapping around its downstream call.

**Next probe (small, and it should settle it):** print `pScreen->DestroyPixmap` immediately after
`DamageSetup`'s `wrap`, and again at `glamor_destroy_gc` entry, alongside the address of
`damageDestroyPixmap`. If they differ, find who assigned it; if `DamageSetup`'s print never
appears, find why the screen has a `pScrPriv` without the wrap. Note glamor's own screen init also
wraps screen hooks, so it is the first candidate for clobbering the chain.

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
