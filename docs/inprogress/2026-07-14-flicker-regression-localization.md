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

## UPDATE 2026-07-14 — measurement harness was broken; now fixed

Verifying the harness surfaced the real reason every prior flicker measurement failed: it was a
**software bug**, not a grabber limitation. `flicker-analyze.py`'s white-pixel counter was
`sum(histogram()[256:])` — a PIL histogram has 256 bins (0..255) and white-after-threshold sits at
index 255, so `[256:]` is an **empty slice → the count was ALWAYS 0**. So the detector reported "no
flicker" for *any* input. The prior-session conclusion "the in-engine scr_capture harness is broken and
the HDMI grab can't self-detect flicker" was actually this bug (the HDMI grab itself works fine —
`scr_capture` (render-to-scanout readback) is separately broken and is NOT needed).

Fixed (`scripts/flicker-analyze.py`, coord `8d87e82`): count via `histogram()[255]`; de-duplicate the
~60fps grab of a ~30fps source (every quake frame is doubled, which broke triplet logic); and replace
the static-camera blink test with a motion/cut-robust one (pixel differs from BOTH neighbours).
Validated on `artifacts/flicker/20260714-173644-harness-verify` (netboot, current build): explosion
onsets correctly score 20–33%.

**Key limitation (now understood):** absolute detection cannot separate genuine flicker from legit
one-frame VFX (explosion onset, muzzle flash) — both differ from both neighbours. In this 28s capture
the top-scoring frames are all legit VFX; the sampled monster/world regions render consistently, so the
specific "monster flicker" was not isolated (the capture also began during console scroll, not demo1).

**Revised path forward:** the harness's real power is **A/B of two builds on the same deterministic
demo** — a regression shows as *more* blink pixels on entity/world regions. So:
1. Capture a clean demo1 window on the CURRENT (flickering) build → baseline blink rate.
2. Build the **full-lightmap-upload discriminator** (ignore `rectchange`; upload the whole lightmap) →
   capture the SAME window → compare. Lower blink rate ⇒ region-tracking bug in the mesa v3d
   texsubimage path; unchanged ⇒ look at the driver coherency / migration delta instead.

## UPDATE 2026-07-14 (evening) — DECISIVELY localized to alias-model per-frame uniform update

Discriminators run on HW (user eyeball, cvar-level, no rebuild except the full-upload one):

- **full-lightmap-upload** (rebuild; verified in binary md5 e593f041): flicker **unchanged** ⇒ NOT the
  world-surface lightmap upload / rect-tracking path.
- **`r_drawentities 0`** (SD rootfs autoexec.cfg): monsters/weapons/buttons vanished **and flicker
  vanished**, world stayed lit + stable ⇒ flicker is on the **alias models (dynamic entities)**, NOT
  the world. (Monsters vanishing also proved the autoexec is read — proof-of-application.)
- Prior A/B: `r_dynamic 0` ⇒ no flicker.

Git (widened to since-06-10) shows the model-lighting code is **unchanged** in the regression window:
only `5e3ec37` (06-24 add lit-floor) + `90da546` (06-26 remove floor + pose-snap), both **excluded**
(pose-snap not r_dynamic-gated; the lit term `mix(dot1,dot2,Blend)` is **LightColor-independent** so a
miscompile there cannot be gated by LightColor/r_dynamic — causally disconnected). `R_SetupAliasLighting`
unchanged (blame: ancient upstream commit).

**Fingerprint (sharp):** a model whose **`LightColor` uniform changes every frame** flickers;
constant-lit models and the world do not. Consistent with ALL observations:
- `r_dynamic 0` ⇒ model LightColor constant ⇒ uniform stable ⇒ no flicker ✓
- `r_drawentities 0` ⇒ no models ⇒ no uniform ⇒ no flicker ✓
- world surfaces don't use the alias LightColor uniform ⇒ no flicker ✓
- dynamically-lit models ⇒ LightColor changes per frame ⇒ flicker ✓

**Conclusion:** the regression is in the **mesa v3d driver** (the 07-03/07-04 materialization), in the
**per-frame uniform (LightColor) update path** for alias-model draws — NOT a quakespasm bug (no
quakespasm rebuild will fix it). The winsys (`v3d_phoenix_winsys.c` submit path, lines ~784–811)
already documents this exact failure mode ("GPU serves stale uniforms from its uniform cache →
per-frame flicker of dynamically-lit surfaces") and applies SLCACTL UCC (uniform-cache) invalidation +
a CPU→device store barrier. Since flicker persists on per-frame-changing model uniforms, the residual
mechanism is most likely a **uniform-stream BO write-ordering / inter-frame reuse race** that
invalidation does not cover (frame N's GPU job still reading the uniform BO when frame N+1's CPU write
overwrites it), OR a delta the mesa migration introduced in uniform-stream BO management.

**Next:** driver-level — inspect mesa v3d uniform-stream BO allocation/reuse + the winsys submit
ordering; candidate fix = ensure per-draw uniform data is not overwritten while a prior job reads it
(fresh/orphaned uniform BO or a proper fence), then HW-eyeball. Deep + uncertain (squashed mesa
history); may be attended-scale.

## SEPARATE bug (orthogonal): mid-render fault-in-fault freeze
During the `r_drawentities 0` run, quake rendered ~42–47 fps then the screen froze on one frame and the
UART flooded with 25+ identical `Exception #37: Data Abort (EL1)`. Registers hold the exception
dumper's own format strings (x8=`"far="`, x17=`" for OFF"`; `far` all-`0x66`) ⇒ a fault WHILE printing
a fault dump = a console/teken fault-in-fault loop, not the quake renderer. Root cause upstream of the
dump loop; not caused by `r_drawentities 0` specifically (demo ran a while first). File separately.
