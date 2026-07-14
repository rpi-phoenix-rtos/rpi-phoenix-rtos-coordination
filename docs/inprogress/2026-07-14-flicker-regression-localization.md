# Quake flicker regression — localization (HW-free pass, 2026-07-14)

**Symptom (user):** parts of the scene, *especially around monsters you aim at*, flicker during
intense action. A build "~2 weeks ago or earlier" did not. Confirmed on 3 builds this session incl.
the clean Docker image (so it is in **committed source**, not a stale binary).

**Established gate (prior A/B):** `r_dynamic` drives it (EZ-off+`r_dynamic 0` = no flicker;
EZ-off+`r_dynamic 1` = flicker). EZ back ON (per user) does not fix it → `r_dynamic` is the real gate,
independent of Early-Z.

## Ruled OUT this pass (all HW-free, with evidence)

1. **Alias mesh 2-pose interpolation** — `GL_DrawAliasFrame_GLSL` snaps `pose1=pose2` (r_alias.c:243)
   since 06-26, so `blend=0` always and the known-bad two-offset fetch never runs. Clean.
2. **Lit-term floor removal** (part of the 06-26 commit) — with `blend=0`, `mix(dot1,dot2,0)=dot1 ∈
   [0.7045,2.0] > 0`, so removing the `max(...,0.7)` floor is a genuine no-op; cannot cause black
   triangles. Dead.
3. **07-10 winsys cleanup `f61397a`** — comment/prose only (verified: dead-debug removal, dup decls,
   stale-comment rewrites). No logic change.
4. **Model dynamic-lighting code** (`gl_rlight.c`: `R_LightPoint`/`RecursiveLightPoint`/`R_MarkLights`)
   — **unchanged** since before the regression window (no commit since 06-10).
5. **GPU depth-drain wedge / drop-frame** — event counts are **flat ~0–2 per session across the entire
   window** (06-26 good-era logs also show 0–1); no frequency increase correlating with the regression.
   The wedge is real but long-standing, not the flicker regression.
6. **Dropped GPU-coherency flush** — SLCACTL slice-invalidation + L2T flush + CPU→device store barrier
   are all present in `v3d_phoenix_winsys.c` (submit path lines 808–813, 918; "essential for
   multi-frame rendering"). Not regressed.

## What DID change in the regression window (post ~06-30)

The quakespasm **game code in the lighting / lightmap / model paths is unchanged**. The only changes to
the GPU/quake stack are in the **mesa v3d driver materialization / GL build**:

- `7f9815c` 07-02 publication prep — de-hardcode tools/, vendor port tarballs, branches→master
- `ee7e0f6`,`f50cc6e` 07-03 build-gl-phoenix — **materialize generated util/format + builtin_types
  sources** ("fixes clean-build state_tracker drop", "fixes clean-build GL/GLQuake")
- `bac7540` 07-04 — **replace host→VM mesa rsync crutch with clone + tracked port patch (#81)**
- `b234aa4` (mesa clone) 07-04 — squashed "Phoenix-RTOS RPi4 port (V3D 4.2 GL)" commit

## Leading hypothesis

The flicker is a **v3d driver-level regression introduced by the 07-03/07-04 mesa
materialization change** (clone+patch and/or regenerated generated-sources), exposed by the
`r_dynamic` rendering pattern (frequent lightmap `glTexSubImage2D` updates + more state churn). It is
gated by `r_dynamic` because only dynamic lighting drives those per-frame texture updates; the game
code that issues them did not change, so the fault is below it, in the driver.

Two sub-mechanisms remain candidates (both consistent with all evidence above):
- (a) a codegen/patch delta in the migrated mesa v3d tree vs. the known-good rsync'd tree;
- (b) an inter-frame WAR hazard on the persistent lightmap BO (frame N+1 CPU `glTexSubImage2D`
  overwrites texels frame N's GPU job still reads) — which neither the CPU-write barrier nor the
  pre-job cache *invalidate* addresses (invalidate fixes stale-read, not still-in-use overwrite).

## Path forward (measured, not blind)

The problem has resisted multiple sessions of blind fixes; the missing capability is **measurement**
(the HDMI grabber locks one frame, so flicker can't be eyeballed via capture).

1. **Verify the `scr_capture` harness** (built, uncommitted in gl_screen.c): one netboot cycle (card is
   in host, CPU free) to confirm it dumps real (non-black) scanout frames to the TCP sink, and that the
   blink detector flags the flicker. This converts every future iteration from a user test-cycle into a
   self-serviced one.
2. **Bisect the 07-03/07-04 mesa materialization** against the known-good pre-07-02 GPU archive using
   the harness, scoped to the v3d driver.
3. **Discriminator if bisect is inconclusive:** full-lightmap-upload (ignore `rectchange`) — robust,
   depends on nothing else; isolates region-tracking vs. driver coherency.

Do NOT ship a coherency fix until the harness confirms the subsystem — see the failed barrier attempt
(`__sync_synchronize` in `ioc_submit_cl`, uncommitted) which ordered the CPU write but did not address
the still-in-use overwrite, and did not help.
