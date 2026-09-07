# X desktop-exit crash — ROOT-CAUSED (2026-09-08)

Superseded the earlier "open, model was wrong" version of this file. The fix is
`tools/x11-port/patches/xorg-server-21.1.24-glamor-stipple-damage-double-register.patch`,
applied by `build-xserver-core.sh` in the same `GLAMOR=1` block as the other two glamor
patches.

Reproduce: `startx_gpu --quit-after 90 deskapps` (that flag exists only for this — the image
ships no `ps`/`pkill` and `xlaunch` blocks in `waitpid`). Fires every run.

## Root cause

`glamor_track_stipple()` (`glamor/glamor_core.c`) is called from `glamor_validate_gc()` on
**every** `GCStipple` change, but the `DamagePtr` is created once and cached in
`gc_priv->stipple_damage`. From the second stipple change on a GC onward it therefore called
`DamageRegister()` on a damage that was **already the head** of that drawable's damage list.

`damageInsertDamage()` prepends unconditionally — its `"Damage already on list"` check sits
behind `DAMAGE_VALIDATE_ENABLE`, which is off in normal builds — so the node self-links:

```c
pDamage->pNext = *pPrev;   /* *pPrev is already pDamage  =>  pNext == pDamage */
*pPrev = pDamage;
```

`damageRemoveDamage()` then unlinks the first match with `*pPrev = pDamage->pNext`, which is
`pDamage` again, so **the list head still references the node after `DamageDestroy()` frees
it**. A later `DamageUnregister()` reads a recycled heap chunk as a `DamagePtr` and
dereferences `pDamage->pDrawable->pScreen`.

Both of damage.c's own guards against exactly this are compiled out (`"Damage already on
list"`, `"Damage not on list"`), which is why it failed silently rather than aborting.

## The evidence, decoded

Fault, stable across every run of the unpatched server:

| field | value |
|---|---|
| process | `/bin/Xphoenix-glamor-daemon`, thread 62 |
| exception | `Data Abort (EL0)`, `esr=0x92000007` (read) |
| `pc` = `lr` | `0x51d444` |
| `far` = `x0` | `0x60` |
| `x1` | `0` |

`0x51d444` is `dixGetPrivate+0x68` in **damage.c's** copy of the static inline (the binary
carries ~110 per-TU copies; the one that crashed is at `0x51d3dc`, which is what identifies the
TU). Disassembled, that instruction is:

```
0x51d440:  bl   51d360 <dixGetPrivateAddr>
0x51d444:  ldr  x0, [x0]        <-- faults
```

So **`far`/`x0` is the *return value* of `dixGetPrivateAddr`, not an argument**, and
`dixGetPrivateAddr` ends:

```
ldr x0,[sp,#24]   ; privates
ldr x1,[x0]       ; x1 = *privates  = pScreen->devPrivates
ldr x0,[sp,#16]   ; key
ldr w0,[x0]       ; key->offset
add x0, x1, x0    ; return
```

Therefore:

* `x1 = 0` is `pScreen->devPrivates` — **the screen's whole dix privates array read as NULL**.
* `far = 0x60` is `NULL + damageScrPrivateKeyRec.offset`.

⚠️ **The previous version of this file read `x1 = 0` as "the private key is NULL" and called it
"the strongest untouched clue". That was wrong** — it is a register left over from
`dixGetPrivateAddr`. The key was fine: `[sp+16]` on the stack is `0x1b83450`, which the symbol
table resolves to `damageScrPrivateKeyRec` itself, and both asserts (`key->initialized`,
`key->size == 0`) passed before the fault.

Call chain, from the kernel's user-stack window `fp`/`lr` pairs, every address landing **inside
a sized symbol** (`nm -S`) and confirmed against the disassembly:

```
doFreeResource            0x42dba4
 FreeGC                   0x41a1b4
  glamor_destroy_gc       0x5b2a0c
   glamor_invalidate_stipple 0x5b2624
    DamageUnregister      0x524228   (damage.c:1796, damageScrPriv(pScreen))
     dixLookupPrivate     0x51d50c
      dixGetPrivate       0x51d444   <-- ldr x0,[x0]
```

The earlier note recorded this frame as `DamageUnregister (+0x524228)`; `0x524228` is an
*address* inside `DamageUnregister` (`0x5241ec`, size `0xf8`), not an offset. `0x524224` is
literally `bl dixLookupPrivate`, so `0x524228` is its return address. The symbolication was
sound; only its typesetting was not.

Objects recovered from the frames (`DamageUnregister` keeps `pDamage` at `sp+24`, `pScreen` at
`sp+56`, `pDrawable` at `sp+64`; its `x29 == sp`):

| local | value | verdict |
|---|---|---|
| `pDamage` | `0x2a59d28` | in the heap |
| `pDrawable` | `0x3ad9c8` | **below the image and outside the heap** |
| `pScreen` | `0x3ad9d8` | `= pDrawable + 16`, and that word points at itself |

The ELF loads `0x400000–0x1baac80`; the heap is above it (`pDamage` is there). A `PixmapRec` is
`malloc`'d, so **`0x3ad9c8` cannot be a live X object** — and a self-pointer at `+16` is
list/rbtree bookkeeping, not a `DrawableRec`. Just below `sp` the same window holds dead
`lib_rbInsert` / `lib_rbRemove` / `_malloc_chunkRemove` frames: a `free()` had just run on this
stack. So `pDamage->pDrawable` was reading allocator metadata in a freed chunk.

## Why the three earlier fixes failed

All three targeted the **glamor screen private allocation**. The broken lifetime is the
**damage's drawable**, reached through a damage that the drawable's list still pointed at after
it was freed — a different object entirely.

1. Defer `free(pScrPriv)` past `CloseScreen` — still crashed, symptom moved to a jump through a
   stale function pointer.
2. Defer + clear the slot + guard `DamageUnregister` on NULL — moved the identical fault to
   `compDestroyWindow`. A NULL guard cannot help: `pDrawable` is non-NULL *garbage*.
3. Never free the screen private at all — **fault unchanged**, which correctly refuted the
   use-after-free-of-a-private model. It just did not point at the right object.

All three were reverted (`ports 21bd0ec`).

## Method note worth keeping

Two independent measurement traps were in play, and both are generic:

* A register in a crash dump is only an argument **at function entry**. At `+0x68` into a
  callee, decode the disassembly before naming what a register holds.
* When a binary carries many per-TU copies of a `static inline`, *which copy* the `pc` lands in
  is what identifies the translation unit. `nm -S` plus a symbol-size bound is reliable;
  nearest-preceding-symbol without a size check is not.
