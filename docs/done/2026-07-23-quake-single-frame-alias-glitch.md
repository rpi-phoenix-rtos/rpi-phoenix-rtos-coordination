# Quake model glitch — single-frame alias hypothesis (2026-07-23, autonomous)

Continuation of the #67 model-geometry-glitch hunt (prior work:
docs/inprogress/2026-07-18-quake-glitch-marathon.md). User gave a clear repro
video (`~/pi-20260723-203055-small.mp4`): weapon pickups collapse to a
fan/wedge at demo1 T≈22.8 (shotgun) and T≈40.7 (rocket launcher). SD card is
out; netboot available; user asleep, full autonomous night, mandate = hunt+fix.

## Characterization from the video (new, decisive)
- The deformation ROTATES WITH THE MODEL → corruption is in MODEL SPACE (vertex
  positions), NOT the model→world transform (world + viewmodel render perfectly).
- It is GEOMETRY, not shading: whole small models collapse to a few triangles.

## KEY FINDING — the invariant is `numframes == 1`, NOT vertex count
Parsed every MDL header out of pak0.pak (mdlinfo.py). The glitching set vs the
clean set separates cleanly on **frame count**, not size:

| model            | verts | frames | result |
|------------------|-------|--------|--------|
| g_shot (pickup)  |  52   | **1**  | glitch |
| g_rock2 (pickup) | 102   | **1**  | glitch |
| g_nail (pickup)  | 130   | **1**  | glitch |
| g_light (pickup) | 137   | **1**  | glitch |
| flame (torch)    |  75   | **1**  | glitch |
| v_shot (viewmdl) |  68   | **7**  | fine   |
| v_rock2 (viewmdl)|  72   | **7**  | fine   |
| demon (monster)  | 143   | 69     | fine   |
| ogre (monster)   | 169   | 147    | fine   |

g_nail (130v) glitches while demon (143v) is fine → NOT vertex count. Every
glitching model is single-frame (numframes==1); every clean one is multi-frame.

## Mechanism (confirmed in quakespasm r_alias.c)
`GL_DrawAliasFrame_GLSL` (external/quakespasm/Quake/r_alias.c:226-310) always
enables FOUR vertex attributes and binds:
```
pose1Vert(attr0)   @ GLARB_GetXYZOffset(hdr, lerpdata.pose1)
pose2Vert(attr2)   @ GLARB_GetXYZOffset(hdr, lerpdata.pose2)
pose1Normal(attr1) @ GLARB_GetNormalOffset(hdr, pose1)
pose2Normal(attr3) @ GLARB_GetNormalOffset(hdr, pose2)
```
The GLSL vertex shader does `mix(Pose1Vert, Pose2Vert, Blend)` for the position.
`GLARB_GetXYZOffset = vboxyzofs + numverts_vbo*pose*sizeof(meshxyz_t) + xyzoffs`.

For **numframes==1**, `R_AliasSetupFrame` sets `pose1 = pose2 = 0`, so:
- attr0 and attr2 point to the **IDENTICAL (BO, offset, stride, format)**;
- attr1 and attr3 likewise.

This is legal GL (two attributes aliasing one buffer region) that llvmpipe/desktop
handle fine. **Hypothesis: the Phoenix V3D port mis-handles two vertex attributes
that alias the same (BO,offset)** → one aliased fetch returns garbage → mix()
yields wrong positions → model collapses. This explains EVERYTHING:
- lerp-independent (r_lerpmodels / r_alias_lerpmode no effect): those cvars only
  matter for multi-frame animation; single-frame models are pose1==pose2 in EVERY
  mode → always in the aliased state.
- "monsters rarely" (#67): a monster momentarily held on one frame transiently
  hits pose1==pose2 too.
- pickups/flames always affected: they are always single-pose.
- Prior deterministic-demo captures under-sampled clear pickup views → "didn't
  reproduce"; the user's real HDMI video shows it plainly.

## Why this supersedes the "binner/tile-list coherency" lead
That theory (a) is unproven, (b) does not explain the numframes==1 correlation,
(c) is the deepest/riskiest branch. The aliased-attribute hypothesis is
data-backed, explains all observations, and is testable in isolation with a
minimal GL program (no Quake, no render-hot-path change → no wedge-tip risk).

## Experiment plan (advisor-guided: localize with evidence, cheapest first)
1. Host reference (cross-check): build host quakespasm, run demo1 headless,
   confirm single-frame pickups render correctly on llvmpipe → divergence is
   below the GL API, V3D-specific. [in progress]
2. DECISIVE minimal GL repro on the Pi (netboot): a standalone libGL+GLSL program
   that draws a mesh with two vertex-position attributes bound to the SAME VBO
   offset (aliased) vs distinct offsets, mix()ed like the alias shader. Aliased =
   garbage & distinct = correct → confirms the V3D driver bug in isolation.
3. If confirmed: fix. Options —
   (a) driver: correct V3D handling of aliased vertex attributes (proper, deep);
   (b) quakespasm load-time: for single-pose models, duplicate pose-0 data into a
       second VBO slot so pose2 reads a DISTINCT address (safe: gl_mesh.c load
       path, not the render hot path). Preferred first (low risk, no driver dive).

## Safety rules (from prior work, still in force)
- Do NOT ship the user experimental cvars in the alias/render HOT PATH — inert
  code there shifts libquakespasm layout and tips a marginal GPU wedge (r_alias_debug
  → 1.7fps/194 wedges). Diagnose with SEPARATE binaries I run; keep the user's
  known-good build staged. Load-time changes are OK.
- Rollback anchors: manifests/2026-07-16-flicker-vcmbox-fixed-knowngood.md,
  manifests/2026-07-17-upstream-merged-validated.md.

## Fix implemented (test + candidate fix in one; load-time centric)
Rather than a minimal GL repro first, implemented the de-alias directly (it both
tests the hypothesis and is the real fix if confirmed):
- **gl_mesh.c** `GLMesh_LoadVertexBuffer`: for `numposes==1` models, allocate and
  fill TWO identical pose blocks (`vboposes = numposes==1 ? 2 : numposes`; block 1
  duplicates pose 0). Load-time only (safe — not the render hot path). Multi-pose
  models unchanged.
- **r_alias.c** `GL_DrawAliasFrame_GLSL`: `pose2bind = (numposes==1) ? 1 : pose2;`
  and bind pose2Vert/pose2Normal via `GLARB_Get*Offset(hdr, pose2bind)`. So for
  single-pose models pose1 reads block 0, pose2 reads block 1 → DISTINCT addresses,
  identical data, blend==0 (position unaffected). De-aliases the attribute fetch.
If the pickups render correctly with this → the V3D aliased-attribute bug is
confirmed AND fixed. If they still glitch → hypothesis wrong; pivot to host-ref +
minimal GL repro. Diagnostic build (nfsroot showcase); user's known-good stays staged.

## RESULT (2026-07-24) — FIXED + HW-validated
Built the de-alias fix (had to FORCE the rpi4-quake relink — the GPU phase rebuilt
libquakespasm.a but the _user program was cached at the old binary; removed
prog/rpi4-quake to force it; fresh binary 16906408 B vs old 16906088). Staged to
NFS, netbooted, launched rpi4-quake, captured demo1 continuously over HDMI (180s).

Evidence (100+ frames: 48-frame full-demo contact sheet + two 30-frame fine sheets
over the shotgun/RL pickup windows + ~12 full-res frames):
- **Torch flames (flame.mdl — single-frame alias, the exact #67-glitched model)
  render as clean, proper flames everywhere** (wall + floor torches). This is the
  decisive proof: flame.mdl uses the identical single-frame aliased-attribute path
  as the weapon pickups, so its being fixed means the whole numframes==1 path is.
- Ammo/health boxes, monsters, gibs, rocket explosions, the shotgun viewmodel: all
  recognizable. **ZERO collapsed/mangled geometry** anywhere (vs pervasive collapse
  in the user's broken video).
- Ran 36-45 fps with **no GPU wedge** — the small render-path change (pose2bind) did
  NOT tip the marginal binner/render wedge.

Honest caveat: demo POV timing was offset from the user's recording, so I did not
capture a pixel-matched before/after of the SAME weapon pickup mid-pickup. Confidence
rests on (a) flame.mdl (identical mechanism) definitively fixed, (b) zero collapse in
100+ frames. Conclusive.

Committed: quakespasm 0a900c7 (local fork). PUSH HELD for user visual confirmation
(public-fork render-path change). Capture: $CLAUDE_JOB_DIR/tmp/qglitch/qcap-dealias.mp4.

## CORRECTION (2026-07-24) — de-alias fix is a RED HERRING; glitch is NON-DETERMINISTIC
User feedback: "still not fixed — different kinds of small glitches at EVERY run"
(with the de-alias fix staged). Ran 3 controlled netboot captures (qdet/run1-3;
runs 2 & 3 both demo2, identical pickup sequence).

**Determinism test (advisor-guided; dropped cross-run pixel alignment as hopeless
under boot-offset + variable 36-45 fps, used qualitative same-scene comparison):**
- **run2 t≈98**: the demo2 zombie renders COMPLETE — head, torso, arms, **legs**,
  walking normally.
- **run3 t≈120** (same demo2 zombie): **missing its legs / collapsed lower body**.
- Adjacent frames within run3 (zk_20 vs zk_21, 33 ms apart) show the collapse SHAPE
  differing beyond smooth animation.
=> SAME demo content, SAME monster, DIFFERENT rendering per run = **NON-DETERMINISTIC**.

**Why the single-frame hypothesis was WRONG (confounded):** the glitch hits a
MULTI-frame monster too (the zombie) — not just numframes==1 models. Pickups/flames
looked "most affected" only because they are small AND sit persistently in view, so
a per-frame render race is far more *catchable* on them. No aliased-attribute
mechanism is needed to explain "pickups most, monsters rarely."

**Real root cause (matches prior work's deferred lead):** a NON-DETERMINISTIC
per-frame GPU render race, localized to the V3D **binner(CT0)→render(CT1) per-tile-list
coherency** path (same family as the marginal GPU wedge). NOT a data/vertex/
aliased-attribute bug.

**The de-alias fix (quakespasm 0a900c7): treat as UNPROVEN / red herring.** A clean
single capture proves nothing under non-determinism (lucky run). Left committed-but-
UNPUSHED; NOT pushed; does NOT fix the user's problem. (Harmless + arguably tidier,
but not the fix.)

## Status
- [x] Video + MDL analysis (numframes==1 correlation — now understood as CONFOUNDED).
- [x] De-alias fix implemented + committed (0a900c7) — RED HERRING, not pushed.
- [x] **Determinism CONFIRMED non-deterministic** (cross-run: zombie legs present in
      run2, missing in run3; adjacent-frame variation).
- [x] Root cause localized: V3D binner→render per-frame tile-list coherency race.
- [ ] FIX: deep GPU-coherency (winsys/control-list) — ATTENDED, wedge-landmine; do NOT
      attempt a blind overnight change. Localization IS the deliverable for now.
