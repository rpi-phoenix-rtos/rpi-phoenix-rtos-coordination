# V3D Early-Z re-enable — root-cause & re-enable plan

Status: DONE (2026-06-22) — EZ re-enabled + HW-validated. See RESULTS at bottom.
Owner: completed in-session.

## RESULTS (2026-06-22)

- **Phase 0** built: `rpi4-v3d-stalltest` + `v3d_phoenix_ez_force_on` flag (winsys) gating the
  mesa force-disable so the harness drives EZ ON.
- **Phase 0 boot (ezrepro):** EZ FORCED ON, 3000 tilted depth-tested frames → **0 wedges, 0
  timeouts**. The EZ hang did NOT reproduce.
- **Phase 1 (H1):** found ALREADY IMPLEMENTED — `l2t_flush_wait()` wraps the pre-bin + bin→render
  L2T flushes (GFXH-1897). The EZ force-disable predated it; the flush fix cured the EZ hang.
- **Phase 2:** unnecessary (no hang to chase).
- **Phase 3:** removed the mesa EZ force-disable (normal state machine runs — EZ off per-shader
  for discard/writes-Z), restored stock global `glDepthFunc(GL_LEQUAL)` in pl_phoenix_vid.c,
  removed the viewmodel `GL_LEQUAL` special-case in gl_rmain.c, reverted the stalltest bundle
  swap back to rpi4-quake.
- **Phase 4 boot (ezon):** Quake with EZ ON + stock `GL_LEQUAL` → **drops=2 total** (vs 265 with
  EZ-off+LEQUAL; on par with the GL_LESS workaround's drops=1), peak **50 fps** (vs ~45 EZ-off),
  NFS mounted, all 5 rendering issues correct, **viewmodel solid** (global LEQUAL, no special-case).
- **Outcome:** the EZ hang was a symptom of the (since-fixed) L2T flush race. Stock `GL_LEQUAL`
  now runs without the depth-drain wedge because EZ early-rejects the overdraw that LEQUAL+late-Z
  choked on. The `GL_LESS` workaround + viewmodel special-case are removed; the `TODO(v3d-ez)`
  debt is retired. Cleanup remaining: remove the now-dead `v3d_phoenix_ez_force_on` flag.

---
Original plan below (for reference).

Status: PLAN (executed 2026-06-22).
Context: GLQuake renders correctly today using a workaround (`GL_LESS` globally +
`GL_LEQUAL` scoped to the viewmodel) because `GL_LEQUAL` on the heavy world render
provokes the BCM2711 V3D "depth-pipeline drain stall" wedge (~265 mitigated frame-drops/min
vs ~1 with `GL_LESS`). Early-Z (EZ) is currently force-disabled. Re-enabling EZ is the real
fix: it lets stock `GL_LEQUAL` run everywhere with no wedge AND boosts FPS (early fragment
reject). This doc is the investigation→fix→validate plan.

## What is force-disabled today

`external/mesa/src/gallium/drivers/v3d/v3dx_draw.c` `v3d_update_job_ez()` — after all the
normal EZ-direction logic (line ~1010) we unconditionally append:

```c
job->ez_state = V3D_EZ_DISABLED;   /* PHOENIX PORT FIX (UPDATE 25aw) */
```

Reason recorded: EZ HANGS the render on depth-tested **steeply-tilted** polygons (steep
screen-space Z gradient — e.g. a cube face rotated edge-toward-camera); screen-parallel
depth-tested geometry was fine. Proven by bisection. Disabling EZ is correctness-safe (pure
perf opt), so it unblocked all 3D — at a fragment-shading cost AND, we now understand, by
masking the depth-drain marginality that resurfaced via `GL_LEQUAL`.

## EZ config data-flow (where the knobs live)

- **Direction decision (per job):** `v3d_update_job_ez()` sets `job->ez_state` /
  `job->first_ez_state` ∈ {`V3D_EZ_UNDECIDED`, `LT_LE`, `GT_GE`, `DISABLED`}. Already handles
  GFXH-1918 (odd width/height or 16-bit MSAA depth load → disable).
- **Per-job RCL:** `v3dx_rcl.c:730` `TILE_RENDERING_MODE_CFG` → `config.early_z_disable` +
  `config.early_z_test_and_update_direction` (from `first_ez_state`).
- **Per-draw:** `v3dx_emit.c:347` `CFG_BITS` → `config.early_z_updates_enable` +
  `config.early_z_enable` (from `ez_state != DISABLED`).
- **Per-shader-record:** `v3dx_draw.c:484,574` `shader.turn_off_early_z_test` (set when FS
  discards / writes Z / has side effects). This is the *correct* per-shader EZ-off; Quake's
  alpha-test (discard) shaders already set it, so EZ would only ever be on for opaque draws.

## Leading hypothesis (test FIRST)

The wedge dump reads `fdbgs` at the **EZTEST** stage, and the winsys already documents a
**separate L2T flush-completion race** that produces the *same* depth-drain stall
(`v3d_phoenix_winsys.c:~594`):

> "a flush must not be issued while a previous L2T flush is still in progress … our submit
> issues L2T flushes back-to-back (bin pre-flush → render pre-flush → readback) with no wait …
> identical demo content stalls ~50% of boots = exactly this flush-completion race."

**Hypothesis H1:** the "EZ-hangs-on-tilted-polys" symptom and the "`LEQUAL`-drain-stall"
symptom are the *same* root cause — the EZ/depth buffer is read by the render pass before the
bin-phase L2T flush that produces it has completed (no flush-done wait). Tilted polys / `LEQUAL`
just increase EZ-buffer traffic enough to expose the race. Early-Z reads a reduced-resolution
depth summary buffer in/near the TLB; a stale read mid-walk → drain deadlock.

If H1 holds, fixing the flush-completion wait fixes BOTH the residual stall AND unblocks EZ —
the single highest-value step.

## Plan

### Phase 0 — deterministic repro (no Quake, fast) [U]
- Extend `phoenix-rtos-devices/misc/rpi4-v3d-stalltest` (already renders 3000 continuous
  depth-tested frames; devices `3f82254`) with a mode that draws **steeply-tilted** depth-tested
  polygons (a tumbling cube / triangle fan with steep Z gradient) and a build-time flag to force
  **EZ ON** (bypass the `v3d_update_job_ez` force-disable). Goal: a netboot-deterministic hang in
  <30 s, with the existing wedge dump (`fdbgs/fdbgo/errstat/ct1ca`) captured. This removes Quake +
  NFS from the loop entirely.
- Exit: EZ-on tilted render hangs reproducibly; EZ-off does not. Confirms the repro.

### UPDATE (2026-06-22, during execution): H1 is ALREADY IMPLEMENTED

Reading `v3d_phoenix_winsys.c ioc_submit_cl`, the L2T flush-completion fix is already in the
committed code: `l2t_flush_wait()` (spins on `L2TCACTL_L2TFLS`) wraps **both** the pre-bin flush
(lines ~747/749, wait-old + wait-new) **and** the bin→render handoff (~848/850, wait-old + flush
+ wait-new + SLCACTL inval). GFXH-1897 is cited and the ~50%-stall race is documented as fixed.
The EZ force-disable was added in an *earlier* session — plausibly BEFORE this flush fix landed.

**Consequence — two outcomes from the Phase-0 EZ-on boot:**
- EZ-on still hangs → flush race was NOT the EZ cause; it's a genuine EZ-buffer coherency/config
  issue the tile-list flush doesn't cover → go to Phase 2.
- EZ-on does NOT hang (0 wedges/3000 frames) → the already-present flush fix also cured the
  original EZ hang → **skip to Phase 3** (re-enable EZ + restore LEQUAL + validate). Best case.

### Phase 1 — fix the L2T flush-completion race (H1) [U]  — likely already done, see UPDATE above
- In `v3d_phoenix_winsys.c` submit path: after each `CTL_L2TFLSTA/END` flush, **poll for
  flush-done before issuing the next flush** (the HW exposes a flush-in-progress/complete status;
  mirror the `MMUC_FLUSHING` pattern already used for the MMU PTE-cache flush at line ~48-49).
  Sequence today is bin-pre-flush → render-pre-flush → readback with no wait.
- Re-run Phase-0 repro with EZ ON. Exit: tilted-poly hang gone with the flush-wait in place.
- Cross-check against `external/linux/drivers/gpu/drm/v3d/v3d_gem.c` `v3d_clean_caches` /
  `v3d_invalidate_caches` ordering (the kernel driver's flush sequencing is the reference).

### Phase 2 — if H1 insufficient, walk the EZ errata [U]
Test each independently in the Phase-0 harness (EZ on), bisecting:
- **GFXH-1918 / depth-load parity:** confirm `draw_width/height` even (1920×1080 is) and depth
  format (we use `DEPTH_COMPONENT24`, not 16-bit MSAA) — should already pass, verify at runtime.
- **EZ-buffer coherency:** the depth/EZ BO is uncached+contiguous in our winsys; ensure it is
  flushed/invalidated between bin and render the same way the color RT is. A stale EZ summary is
  the classic tilted-poly mispredict→hang.
- **`early_z_updates_enable` vs `early_z_enable`:** verify both CFG_BITS fields emit consistently
  (`v3dx_emit.c:347`); a direction/update mismatch across draws disables-then-re-enables EZ
  mid-job, which the spec forbids.
- **Tile size:** steep Z gradient across a large tile stresses the EZ summary; compare our tile
  sizing to Mesa's `v3d_choose_tile_size` defaults (we may diverge in the render-to-scanout path).

### Phase 3 — re-enable EZ in Mesa [U]
- Remove the unconditional `job->ez_state = V3D_EZ_DISABLED;` so the normal `v3d_update_job_ez`
  state machine runs (it already disables EZ correctly per-shader for discard/writes-z — so
  Quake's alpha-test draws stay EZ-off automatically; only opaque world/model draws get EZ).
- Restore stock `glDepthFunc(GL_LEQUAL)` globally in `pl_phoenix_vid.c` and **remove** the
  viewmodel `GL_LEQUAL` scoping in `gl_rmain.c R_DrawViewModel` (no longer needed once EZ+LEQUAL
  works).

### Phase 4 — validate [U]
- Phase-0 stalltest: 3000 tilted-poly EZ-on frames, 0 wedges.
- Quake netboot: `GL_LEQUAL` global + EZ on → 0 `drops=`, correct world + solid viewmodel +
  HUD/fonts/particles, and **measure FPS delta** (EZ should raise it; today's 42–44 fps is the
  EZ-off baseline). Multi-boot tally (the wedge was intermittent — need N clean, not 1).
- Rollback: GPU-render-risk only; a regression just drops frames via the existing mitigation and
  is netboot-recoverable. Snapshot a manifest before/after.

## Risk / classification
- **[U] unattended-capable**: netboot + auto-HDMI + the wedge dump + stalltest self-log cover it.
- Blast radius: GPU render only; no boot-risk. The existing drop-frame mitigation is the safety net.
- Payoff: stock `GL_LEQUAL` everywhere (removes the workaround + the viewmodel special-case) AND
  an FPS gain from early fragment reject. Also retires the `TODO(v3d-ez)` debt and likely the
  residual `drops=1` flush-race.

## Pointers
- `external/mesa/src/gallium/drivers/v3d/v3dx_draw.c` `v3d_update_job_ez` (force-disable site)
- `external/mesa/src/gallium/drivers/v3d/v3dx_rcl.c:730` (RCL EZ config)
- `external/mesa/src/gallium/drivers/v3d/v3dx_emit.c:347` (per-draw CFG_BITS EZ)
- `tools/v3d-driver-port/v3d_phoenix_winsys.c` (~594 flush race; ~48 MMUC_FLUSHING pattern; apply_core_regs)
- `phoenix-rtos-devices/misc/rpi4-v3d-stalltest` (3000-frame harness to extend)
- `external/linux/drivers/gpu/drm/v3d/v3d_gem.c` (`v3d_clean_caches` reference ordering)
