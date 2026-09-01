# Quake entity-flicker regression — investigation (2026-07-12)

## Symptom (user report)
During INTENSE combat (lots of shooting + movement), parts of the scene — esp.
the monsters the player aims at — FLICKER (blink out/in across frames). Calm
scenes are fine. Confirmed REGRESSION: a build ~2 weeks ago (≤~2026-06-28) did
not have it. X11 is fine.

## What was ruled out (subagent + log analysis)
- **Present/off-thread-blit race**: dead. Logs show `readpx=0.00ms` → the direct
  scanout page-flip path is live; the off-thread blit (`reblit_fn`) only runs in
  the CPU-fallback path, which is NOT active. The `(blit+fb0 off-thread)` string
  prints unconditionally, so it isn't evidence.
- **Binner (tile) overflow**: dead. The OUTOMEM servicer hands the whole 32 MiB
  pool and logs `EXHAUSTED` if dry — zero across all logs. Can't silently drop.
- **Wedge/drop-frame mitigation**: dead as *sustained* flicker — ~1 wedge/session,
  and it drops the WHOLE frame (scene-wide hitch), not per-entity.
- **TFU / L2C fence commits (post-06-28)**: irrelevant to GLQuake (those are
  V3DV/Vulkan; the reverted L2C invalidate was already reverted in the good build).

## Objective repro is UNAVAILABLE (important)
The deterministic frame-capture harness (`scr_capture` cvars → TCP sink,
`scripts/quake-capture-sink.py`) was validated 2026-06-15 but is now **broken by
the 2026-06-21 render-to-scanout + triple-buffer page-flip rework**: with the
render going directly to the scanout back buffer, the `scr_capture` pixel
readback (`readpx=0.00ms`) reads an empty/wrong buffer. Captured frames are black
noise (verified: 100 frames, 0 inter-frame motion, garbage content). So I cannot
diff consecutive frames to detect the flicker here. Fixing the capture readback
for the scanout path is a prerequisite for any objective/regression-test of this.

## Bracket + leading hypotheses
No quake/v3d **rendering** commits since 2026-06-28, yet it regressed — so the
regression is either the 06-21/22 rework or a NON-rendering change after ~06-28.

1. **Early-Z re-enable (`e9b9389`, 2026-06-22).** Top suspect by elimination, but
   the EZ code is stock-correct Mesa — if it's the cause it's a *premise* error
   (EZ validated only via a stalltest + eyeball, never pixel-correctness under
   heavy overlapping entity geometry). **Weakened by timeline**: interactive
   aiming needs the mouse, which landed 2026-06-24 — *after* EZ was re-enabled. So
   a combat-capable good build (06-24…06-28) would ALREADY have had EZ on, meaning
   EZ can't be the regression *unless the good build was pre-06-22 (demo-watching,
   EZ off)*. This is the key ambiguity.
2. **Dynamic lightmap / dlight coherency during combat.** Muzzle flashes +
   explosions near the monster you shoot drive per-frame dynamic-lightmap uploads;
   a GPU coherency (stale-read) bug there flickers exactly the lit geometry you're
   fighting. Fits "the monster you aim at while shooting" better than EZ, and it's
   load/combat-dependent. Candidate if the good build was post-06-24.
3. **Post-06-28 build/mesa-provenance change.** `bac7540` (07-04) switched mesa
   from an rsync of the dev-host working tree to a clone pinned at fork commit
   `b234aa4ed9d` + tracked patch; `ee7e0f6`/`f50cc6e` (07-03) changed how mesa
   generated sources are produced. If any of these differ from what built the good
   binary, rendering could shift with zero "rendering commit". The GL patch file
   list looks complete (9 files = superset minus Vulkan), but the mesa base/gen
   provenance vs the good build was not verifiable from here.
4. MAX_VISEDICTS/MAX_DLIGHTS (stock engine limits): least likely — drops the
   *last-added* entities, not specifically the one you aim at; and not a regression
   (the port never changed them).

## Blocked on: pinning the good build
The single fact that adjudicates #1 vs #2/#3: **the date + play-mode of the good
build**. Pre-06-22 demo-watching ⇒ EZ was off ⇒ EZ is the regression (fix: revert
the EZ portion of e9b9389 — re-add the `V3D_EZ_DISABLED` force-off, restore global
`GL_LESS` + viewmodel `GL_LEQUAL`). Post-06-24 interactive ⇒ EZ exonerated ⇒
bisect the dynamic-lighting path and the 07-03/07-04 build/mesa provenance.

## Next steps once the good build is pinned
- If a commit/manifest is known: diff the rendering-relevant state (mesa
  base+patch+gen sources, EZ config, depth func) between then and HEAD → the diff
  IS the regression.
- Independently worth doing: fix the `scr_capture` readback for the scanout path
  so flicker becomes objectively regression-testable (consecutive-frame diff).
