# Model-gallery test environment — plan (2026-07-28)

Goal: a deterministic, comprehensive test that renders EVERY alias model individually and
tells us — with certainty, not eyeballing a live demo — which models render correctly on
the Pi's V3D vs a known-correct reference. Use it to find ALL broken models (weapons,
monsters, torches, items), drive fixes to green, then verify the fixes in the real game.

Motivation: diagnosing #67 bug-2 (nailgun) in the live game failed repeatedly — demos don't
frame pickups, ad-hoc harnesses render through non-game paths and LIE (a viewmodel-swap
collapsed even the known-clean grenade launcher). We need a faithful, repeatable instrument.

## Design (hardened per advisor 2026-07-28)

### Faithfulness is the #1 rule
Render each model through the UNMODIFIED game path: load a real map, spawn ONE entity,
set its `model`, and render via the normal `R_RenderScene` → `R_DrawEntitiesOnList` →
`R_DrawAliasModel` (normal `R_SetupGL` projection + `R_RotateForEntity`, world lighting via
`R_LightPoint`). NO hand-synthesized GL setup, NO viewmodel depth-hack/fov path — those are
how a harness reintroduces lying. Pose comes from the normal `R_SetupAliasFrame`.
- Implementation: gallery mode overrides the client's visible-entity list each frame with a
  single test entity at a fixed origin in front of a fixed camera; the world still renders
  (faithful setup + deterministic CPU lighting), the one test entity is centered.

### Two poses per model
- STATIC pass: entity at a fixed frame → `pose1==pose2` → blend==0 (the bug path).
- ANIMATING pass: two adjacent frames mid-lerp → blend!=0.
Per-model verdict table {static: ok/broken, animating: ok/broken}. This avoids false-positives
(animated monsters only ever draw at blend!=0 in-game; flagging their static pose broken would
be misleading unless labeled) and confirms the blend==0 mechanism.

### Comparison = COVERAGE MASK, not shaded pixels
The confirmed bug is SHAPE (collapsed/missing triangles). llvmpipe vs V3D shade/filter/dither
differently even when both correct → SSIM on lit images has a noise floor. So add a
fullbright-white render style: model = solid white, background dark → threshold → a coverage
mask (which pixels the model covers). Collapsed geometry moves coverage massively; shading
noise doesn't move it at all. Diff = |coverageP i XOR coverage_host| / coverage_host. Auto-flag
models over a threshold; eyeball only the flagged ones.

### Reference = host quakespasm + llvmpipe (headless)
Same quakespasm source + same GLSL alias shader, built for x86 Linux with
`SDL_VIDEODRIVER=offscreen` + Mesa llvmpipe (libEGL + swrast_dri present; no Xvfb needed).
Any Pi(V3D)-vs-host(llvmpipe) coverage diff is a V3D defect. MUST verify `gl_glsl_alias_able`
is true on host (llvmpipe advertises the GL/GLSL version quakespasm's alias path needs) —
else host silently takes a different codepath and the comparison is invalid.

## Sequence (host toolchain NOT on the critical path)
STEP 1 — CALIBRATION GATE (build first, gate everything on it):
  Pi gallery on ~4 models: g_rock, g_nail, one monster (ogre), one torch (flame).
  HARD GATE: the Pi gallery MUST show g_rock CLEAN and g_nail BROKEN (matches user ground
  truth). If g_rock shows broken → harness unfaithful, STOP and fix. If g_nail shows clean →
  the gallery doesn't reproduce the bug, RETHINK before investing. (coherent-weapon vs
  collapsed-spikes is a gross, reliably-judgeable difference.)
STEP 2 — FULL PI SWEEP: all ~62 alias models, static+animating. Read blatant collapses
  directly (no reference needed) → first broken/clean table.
STEP 3 — HOST REFERENCE: headless llvmpipe gallery → reference images; coverage-mask diff for
  the subtle cases + the "100% everything correct" guarantee.
STEP 4 — FIX + ITERATE: with the mechanism finally observable, form + test fixes until the
  gallery is green on Pi (matches host for every model/pose).
STEP 5 — GAME VERIFY: apply the fix to the shipping renderer; confirm in-game (user oracle
  for final sign-off) that nailgun/torches/etc. render correctly and nothing regressed.

## STEP 2 RESULT — DISCRIMINATOR FOUND (2026-07-28): single-pose VBO > 4096 bytes
Block-code sweep covered idx 0–25 (gallery then hit the SEPARATE known V3D binner-wedge at
idx26 h_ogre: `BIN TIMEOUT ... mmu_ill=0x8002ba01 ... GPU wedged`, froze the loop). Robust
block-code attribution is PERFECT (sequential, unambiguous). Cross-referencing on-screen
collapse with `#MGdiag` nvvbo, restricted to single-pose (nposes==1) models:
- BROKEN (dark/collapsed): g_light nvvbo=227, g_nail=217, g_nail2=228
- CLEAN: g_rock=165, g_rock2=164, g_shot=90, armor 70, backpack 108, bolt 29, … (all ≤165)
Current single-pose VBO size = nvvbo*24 bytes (the #67 DUP-BLOCK hack sets vboposes=2 → 2 pose
blocks + 1 st block, each 8B/vert). Boundary is EXACT at 4096 (one 4KB page):
- CLEAN g_rock 165*24=3960 (<4096); BROKEN g_nail 217*24=5208, g_light 5448, g_nail2 5472 (>4096)
- EVERY clean single-pose model <4096; EVERY broken one >4096. Multi-pose models (boss nvvbo=501
  etc.) are clean because pose0 lives in page 0 and their layout differs.
=> ROOT CAUSE (hypothesis, high confidence): V3D mis-maps/mis-fetches a single-pose alias VBO
once it spans >1 page, collapsing geometry. The DUP-BLOCK hack itself pushes big single-pose
models over 4096. That hack is now DEAD CODE: pose2 is disabled at blend==0, and nposes==1 is
ALWAYS blend==0, so block 1 is never bound. FIX: remove the dup-block (vboposes=hdr->numposes),
making single-pose VBO nvvbo*16 → g_nail 3472, g_light 3632, g_nail2 3648, all back under 4096.
Predicted: all three become CLEAN with zero regression (fix is also a legitimate simplification).

## STEP 4 RESULT — FIX VERIFIED GREEN, 61/61 (2026-07-28)
Applied the fix (gl_mesh.c: `vboposes = hdr->numposes`, drop the dup-block; r_alias.c:
`pose2bind = (numposes==1)?0:...`). Rebuilt (quake md5 2d596297), re-ran the block-code gallery.
- The three previously-broken single-pose weapons g_light(12)/g_nail(13)/g_nail2(14) now render
  BRIGHT + COHERENT. The g_nail2 black SPIKE is gone. suit(46) — the borderline single-pose
  nvvbo=237 case — also CLEAN (237*16=3792 < 4096; it would have broken under the old *24 layout).
- FULL SWEEP 61/61: every alias model renders coherent. No black-spike collapse anywhere.
  (backpack/bolt2 showed a 1-frame brown-fill only at the loop-wrap transition; later loops render
  them correctly — a decode-rep artifact, not a defect. Confirmed via tmp/inspect_strip.png.)
- Max single-pose nvvbo across all 61 = suit 237, so EVERY single-pose VBO is now <4096 → the
  nvvbo>256 residual-risk case (256*16=4096) does not exist in the Quake data set.
- Bonus: the intermittent V3D binner-wedge (BIN TIMEOUT/mmu_ill) did NOT block the fixed runs; it
  is HW-marginal and hit a different model each run (h_ogre, then wizard) — a SEPARATE issue.
Evidence: tmp/fix1_montage.png (58/61), tmp/tail_montage.png (61/61).

## Status
- [x] Step 1: Pi gallery + calibration gate — PASSED 2026-07-28
- [x] Step 2: full Pi sweep — discriminator found: single-pose VBO (nvvbo*24) crossing 4096
- [x] Step 3: (host reference deferred — the Pi sweep alone gave an unambiguous 61/61 verdict)
- [x] Step 4: fix to green — 61/61 coherent on Pi (quake md5 2d596297)
- [x] Step 5: committed (quakespasm 3d742a3 fix + 3b1b4af harness; coord e1cb6b3); binary deployed.
      Normal-game HDMI beauty-shot blocked by intermittent NFS flakiness (race / -34) across 3
      cycles — one (ingame2) DID launch and render the game at ~24fps with the fixed binary
      (QSFPS in UART), confirming the normal game runs. Per established practice the USER is the
      reliable in-game oracle; the faithful gallery (identical R_DrawAliasModel path) is the
      authoritative model-correctness verification. autoexec left as `map start` so the user
      lands in the intro hub (nailgun pickup on a pedestal) to eyeball.

## FIX SUMMARY (for the reader)
- Bug #67: nailgun/lightning-gun/nailgun2 pickups collapsed to black spikes on V3D.
- Root cause: the earlier "de-alias" fix allocated a duplicate pose block for single-pose
  models (vboposes=2), inflating the VBO to nvvbo*24; V3D mis-renders a single-pose alias VBO
  once it exceeds one 4KB page. The dup block was dead (Pose2 is disabled at blend==0).
- Fix (quakespasm 3d742a3): `vboposes = hdr->numposes` + `pose2bind=(numposes==1)?0`. Single-
  pose VBO → nvvbo*16; max single-pose model (suit, 237) = 3792 < 4096. 61/61 models coherent.
- Evidence: tmp/before_after.png, tmp/tail_montage.png (61/61), tmp/fix1_montage.png.
- [ ] Step 3: host reference + coverage diff
- [ ] Step 4: fix to green
- [ ] Step 5: game verify

## STEP 1 RESULT — CALIBRATION GATE PASSED (2026-07-28)
The faithful gallery (real map + unmodified R_RenderScene path, single test entity, fixed
camera, fullbright, static frame 0, isolated on dark bg, per-model size-normalized) was run
on the Pi over g_rock/g_nail/ogre/flame. Time-ordered cycle = green-weapon → black-spike →
ogre → torch, mapping exactly to g_rock → g_nail → ogre → flame. Result:
- g_rock (grenade launcher): COHERENT weapon — CLEAN. ✓ (matches user)
- g_nail (nailgun): COLLAPSED into a black-triangle SPIKE — BROKEN. ✓ (matches user)
- ogre (326 tris, numframes=147, rendered at frame 0 => blend==0): COHERENT — CLEAN.
- flame (torch): coherent flame — CLEAN here (user saw "some torches" broken in-game; will
  include flame2 + more in the sweep).
=> The harness is FAITHFUL (reproduces the known clean+broken ground truth) and reproduces
the bug deterministically in isolation, every loop. This is the reliable instrument the whole
investigation lacked. Evidence: artifacts/qglitch-67/2026-07-28-gallery/calib_g_nail_BROKEN.png
(collapsed spike) + calib_g_rock_CLEAN.png (coherent). Gallery cvars/cmd: `mg` (arm, loops),
`mg_tga 1` (write files — host only; NOT Pi NFS). Autoexec: `map start` + `mg`.

KEY MECHANISM UPDATE: ogre CLEAN at blend==0/326-tris DISPROVES "blend==0 + high tri count"
as the trigger. The collapse is more specific to g_nail's class. The full sweep (Step 2) will
reveal exactly which models collapse; then the shared property points at the real cause.

## STEP 2 — INSTRUMENT HARDENING (2026-07-28)
The colour-plateau HDMI decode kept drifting ±1-2 labels (extra/merged plateaus). Replaced the
background-colour index ramp with a robust **6-bit BINARY BLOCK CODE** drawn in the 2D pass
(`R_ModelGallery_DrawTag`, gl_rmain.c): a white ANCHOR block + 6 white/dark data blocks (LSB
first) at the top of every frame. Gamma/downscale/colour-crosstalk immune; auto-calibrates to
any capture resolution via the anchor. Decoder: tmp/decode_blk.py (PIL-only, anchor pitch).
Also added a per-model UART line `#MGdiag[idx] name nvvbo nidx nposes skin` so each on-screen
collapse can be correlated with the exact VBO layout. quake ELF md5 d534173c.

### Offline MDL-header analysis (pak0, tmp/mdlhdr.py) — the g_rock/g_nail discriminator
The single-pose fix (r_alias.c: disable Pose2 at blend==0) FIXED g_rock but NOT g_nail, though
both are single-frame weapons on the identical draw path. The pose1 fetch (4×UBYTE, stride 8,
offset 0) is byte-identical for both => the collapse is DATA-dependent, not code-path dependent.
MDL headers (clean vs broken, all single-frame, same flags 0x08):
- g_rock (CLEAN):  nverts=102 ntris=176 skin 224x195
- g_nail (BROKEN): nverts=130 ntris=222 skin 308x94
- ogre  (CLEAN, multi-pose@frame0 => same blend==0 path): nverts=169 ntris=326 skin 264x194
=> NOT vertex/tri COUNT (ogre bigger than g_nail, still clean). Candidate discriminators left:
numverts_vbo (post-seam-remap, measured via #MGdiag), skinwidth (g_nail 308 vs g_rock 224 →
pad 512 vs 256), skin aspect (g_nail 308x94 ≈ 3.3:1). Sweep + #MGdiag will pin which one tracks
the collapsed set. Candidate fix independent of exact mechanism: upload alias positions as a
more robust vertex format (SHORT/FLOAT) instead of 4×UNSIGNED_BYTE, if V3D byte-attr fetch is
the culprit.
