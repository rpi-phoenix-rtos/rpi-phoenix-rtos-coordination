# X desktop-exit crash — OPEN, and my model was wrong (2026-09-08)

Read this before attempting a fourth fix. Three were tried on hardware and all three failed;
the third disproves the model the first two were built on.

## What is reliably known

Reproduce with: `startx_gpu --quit-after 90 deskapps` (the `--quit-after` flag exists solely
for this — the image ships no `ps` and no `pkill`, and `xlaunch` blocks in `waitpid`, so the
teardown cannot be driven any other way). It fires on **every** run.

The teardown genuinely executes:
`--quit-after fired -> SIGTERM to client[0] (WM) -> client[0] exited (status=0xf00) ->
session ended (WM/last client exited) -> shutting down X`.

Fault, stable across runs of the unpatched server:

| field | value |
|---|---|
| process | `/bin/Xphoenix-glamor-daemon`, thread 62 |
| exception | `Data Abort (EL0)`, `esr=0x92000007` (a **read**) |
| `pc` = `lr` | `0x51d444` = **+104 into `dixGetPrivate`** (real out-of-line symbol, not an inline artefact) |
| `far` = `x0` | **`0x60`** — the *privates* pointer handed to `dixGetPrivate` |
| `x1` | **`0`** — the private **key** is NULL |

Caller chain from the kernel stack window's fp/lr pairs, symbols resolved by **nm bounds**, not
addr2line adjacency: `dixGetPrivate` <- `dixLookupPrivate` (+60) <- varies by run:
- `DamageUnregister` (+0x524228), itself called from **`glamor_invalidate_stipple`** (+68,
  `glamor/glamor_core.c`);
- **`compDestroyWindow`** (+276, the Composite extension).

Impact is narrow: shutdown only, after a recording would end. psh resumes, the box stays up,
and **none of the 21 gated boots are affected** — the shipped image was never gated on it.

## The three failed fixes (do not repeat these)

1. **Defer `free(pScrPriv)` past the downstream `CloseScreen`.** Still crashed. Changed the
   symptom only: the read fault became a **jump through a stale function pointer** (PC alignment,
   `0xaa1403e0aa1503e2` = two AArch64 instruction words), because `DamageUnregister` makes an
   indirect call through `pScrPriv->funcs.Unregister`.
2. **Defer + clear the private slot + guard `DamageUnregister` on NULL.** Fixed that caller and
   moved the identical fault to `compDestroyWindow`. Guarding callers one at a time does not
   converge.
3. **Never free the screen private at all.** **Unchanged fault.** This is the decisive one: if
   the allocation is never freed, a freed screen private cannot be the cause. The whole
   use-after-free model is wrong.

## What the evidence actually points at, unexplained

`x1 = 0` — a NULL **key** — was never explained by the use-after-free model and is the strongest
untouched clue. `damageScrPrivateKey` is `&damageScrPrivateKeyRec`, a static, so it can never be
NULL. Either the call arrives with junk arguments (an indirect call through a corrupted pointer
that happens to land in `dixGetPrivate`), or the key comes from a *stored* location that has been
zeroed. `privates == 0x60` likewise says the base object was NULL and `offsetof(devPrivates)` is
0x60 — which suits a `PixmapRec`/`GCRec`/`WindowRec`, **not** a `ScreenRec` (whose devPrivates
sits far later in a much larger struct). So the object being looked up is probably **not the
screen**, which is another reason the screen-private story never fit.

## Suggested next step

Do not patch on a hypothesis again. Put a **hardware watchpoint** on the location once its
address is known, or better: the kernel already has one —
`hal_wpTrapLo`/`hal_wpTrapHi` with a handler that prints `pc=writer` plus a backtrace
(`hal/aarch64/exceptions.c`). Alternatively widen the kernel stack window further and walk more
frames to find who calls `dixLookupPrivate` with a NULL key.
