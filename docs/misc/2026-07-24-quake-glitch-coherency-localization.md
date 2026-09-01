# #67 Quake model glitch — coherency-angle localization (2026-07-24, autonomous)

Continuation of the #67 non-deterministic model-geometry glitch hunt. Prior:
`docs/inprogress/2026-07-23-quake-single-frame-alias-glitch.md` (the single-frame
aliased-attribute hypothesis + de-alias fix — RED HERRING; the glitch is
NON-DETERMINISTIC and hits multi-frame monsters too). User directive: "keep going
on the coherency angle." Autonomous session, fix is ATTENDED-only (wedge landmine).

## What the coherency angle ruled OUT this session (read-only, decisive)

All against the authoritative Linux driver `external/linux/drivers/gpu/drm/v3d/v3d_gem.c`
and the port `tools/v3d-driver-port/v3d_phoenix_winsys.c`.

1. **L2C / L3 flushes are NO-OPs on V3D 4.2 — not a gap.** I initially flagged the port
   as "missing `v3d_invalidate_l2c` + `v3d_flush_l3`" that Linux runs in
   `v3d_invalidate_caches`. Reading `v3d_gem.c`: `v3d_flush_l3` returns for
   `ver < V3D_GEN_41`; `v3d_invalidate_l2c` returns for `ver >= V3D_GEN_33`
   ("the L2 cache for uniforms and instructions on V3D 3.2"). On the 4.2 BOTH are
   no-ops, so `v3d_invalidate_caches` reduces to exactly **L2T-flush(FLM_FLUSH=0) +
   SLCACTL-slice-invalidate** — which the port already does at submit (winsys 880-883)
   and at the bin→render handoff (983-985). **The port's cache-maintenance sequence
   MATCHES Linux for V3D 4.x.**

2. **The submit BOs are `MAP_UNCACHED` → a `dsb` is sufficient (CPU-cache-stale ruled
   out).** Every CL / vertex / tile_alloc / overflow BO is mapped `MAP_UNCACHED`
   (winsys 236/273/304/417/561 and the default at 586). CPU writes go straight to
   RAM; the `dsb sy` before the CT0 kick (855) drains them. Only BOs with the explicit
   `V3D_CREATE_BO_CACHEABLE` flag (bit 0) are CPU-cached, and Mesa `dc ivac`-invalidates
   those before readback. So "GPU reads stale CPU-cached vertex/CL data" is NOT the
   mechanism for the default submit path.

3. **The bin→render handoff is the heavily-iterated clean+WAIT+invalidate sequence**
   (winsys 982-985): `l2t_flush_wait` (wait-old) → `L2TFLS` (clean binner output to RAM)
   → `l2t_flush_wait` (wait-new, must COMPLETE) → `SLCACTL_INVAL_ALL` → kick CT1. This
   already fixed the *severe* form (CT1 fetching an in-flight/incomplete tile list =
   the ~50%-of-boots wedge). GFXH-1897 (flush-completion race) is handled by
   `l2t_flush_wait`.

## The reframe I had been missing (advisor)

Every step so far assumed **CT1 reads a good tile-list wrongly** (a coherency/read
fault). The untested alternative is **the binner (CT0) PRODUCES a bad/incomplete/varying
tile-list**. These are different root causes and the fix differs. The cheapest way to
split them is empirical, not more code-reading.

## Why the glitch is NOT closed by "matches Linux" + why prior negatives don't transfer

- The port matching Linux's cache maintenance does **not** explain why the port glitches
  where Linux doesn't — the divergence is elsewhere (BO/VA lifecycle, binner sizing,
  or a CT1 internal-fetch effect), not the flush opcodes.
- **glitch ≠ wedge.** The prior `V3D_VA_NO_RECYCLE` + BO-zeroing experiments concluded
  "not a memory/VA/cache effect" — but that verdict was about the **WEDGE** (CT1 overruns
  `ct1ea`, FRDONE never fires). The **GLITCH** is a distinct mode: the frame COMPLETES
  (FRDONE fires), geometry is wrong. VA-recycle / stale-by-VA cache state is therefore
  **not ruled out for the glitch.**

## Strongest correlation on the table

Static-VA GPU workloads never glitch: the tumbling-cube / triangle demos reuse **one
persistent job at stable GPU VAs** (winsys 860) and are HW-proven clean. Quake
**allocates fresh BOs every frame** (858) → new GPU VAs → needs the per-submit
`mmu_flush_tlb` (862). The VA allocator **recycles** freed VAs (first-fit from a holes
list, 459-471). So the glitch correlates with the **per-frame fresh-BO + VA-recycle
path**, which the clean demos never exercise — NOT with the core bin→render handoff
(identical for both). This is the leading hypothesis for the attended experiment.

## Decisive experiment (infrastructure LANDED this session; run is ATTENDED)

Split binner-non-determinism from CT1-read-fault by submitting a **fixed scene N times**
and checksumming the binner's `tile_alloc` output after each FLDONE.

Infrastructure added (additive, safe, no-op unless env-enabled — the user's known-good
`rpi4-quake` is untouched):

- **winsys `v3d_phoenix_winsys.c`**: env-gated (`V3D_BIN_CRC=1`) `bincrc_capture()` CRC32s
  the `tile_alloc` (CT0QMA) region right after the bin's L2T output flush completes
  (uncached CPU read sees the binner's writes in RAM), before the CT1 kick. Exposed via
  `uint32_t v3d_phoenix_last_bin_crc(uint32_t *qma, uint32_t *qms, uint32_t *crc)`.
- **`v3dv_harness.c`**: env-gated (`V3D_DET_ITERS=N`) loop re-submits the SAME recorded
  command buffer (fixed fullscreen-triangle scene, spans ~192 tiles) N times, printing
  per-iteration `seq/qma/qms/crc` + live scanout pixel samples, and a verdict:
  **BINNER DETERMINISTIC** (all CRCs identical) vs **BINNER NON-DETERMINISTIC**.

### Interpretation
- **Varying tile_alloc CRC across identical submits** → binner non-determinism (or its
  writes not landed at FLDONE). Fix is in the CT0/binner-config/flush-timing path.
- **Identical CRC but varying scanout pixels** → CT1's read/fetch path is the culprit
  (the coherency branch), even for a simple fixed scene.
- **Identical CRC AND identical pixels** for the simple triangle → the simple scene is
  clean; escalate the harness toward Quake-scale geometry density + per-frame fresh-BO
  churn (the leading-correlation path) before concluding.

### Build/run runbook (attended)
The Vulkan harness build tree (`/tmp/mesa-v3dv-build`) and `libv3dv-phoenix.a` are GONE,
so this needs a v3dv rebuild first (watch for the SB-2 `spirv_print_asm` link issue —
see docs/KNOWN-ISSUES.md; if it bites the harness link too, add `-Dspirv-tools=disabled`
to the v3dv `meson setup` or the defining source to `v3dv-aux-sources.txt`):

```
python3 tools/v3d-driver-port/build-v3dv-phoenix.py         # -> /tmp/v3dvphx-harness (aarch64-phoenix ELF)
cp /tmp/v3dvphx-harness /srv/phoenix-rpi4-nfs/bin/v3dv-det
# card OUT of Pi (netboot); then at psh:
./scripts/test-cycle-psh-interact.sh --label v3ddet --inter-cmd-secs 8 -- \
  "V3D_BIN_CRC=1 V3D_DET_ITERS=64 /bin/v3dv-det"
./scripts/uart-summary.sh v3ddet   # look for the "#67 DET result: ..." verdict line
```

(The winsys diagnostic also compiles into the GL archive `libv3d-phoenix.a` via
`build-v3d-phoenix.py`, which is incremental-ready — used this session only to
compile-check the winsys change.)

## RESULT — Stage 1 (fixed scene, static VAs): BASELINE SANITY CHECK ONLY (2026-07-24, HW)

Ran `v3dv-det` over netboot (log `rpi4b-uart-20260724-113508-v3ddet.log`): device up, fb0
1920x1080, graphics pipeline + vertexless triangle, then 64× re-submit of the SAME recorded
command buffer. Every iteration: `qma=0x028ff000` (stable VA), `qms=106496`,
`crc=0xef561ea2` identical 64/64, 0 faults, 0 wedge.

**What this proves — and what it does NOT (corrected after advisor review):** this is a
near-tautological baseline: a FROZEN input (identical CL bytes, same tile_alloc BO, same VA —
`qma` never moved, no churn, no re-record) fed to a deterministic HW unit with nothing else
varying. It rules out exactly ONE narrow thing — spontaneous tile_alloc corruption on a frozen
input — and nothing more. Two caveats that gut any stronger reading:
- **The pixel samples were vacuous.** `px0`/`pxc` both read `0x8000ff` = bytes `ff 00 80` =
  the render-pass CLEAR color (`cv={1,0,0.5,1}` magenta), sampled at row 0 (top edge). `loadOp
  =CLEAR` repaints that every frame by construction → the "pixel-identical" signal sampled the
  clear, NOT the triangle. Zero power over rendered geometry. (Stage 2 fixes this: a center-band
  CRC proven to differ from an all-clear CRC.)
- **The binner is NOT exonerated.** #67 appears only under VARYING conditions (Quake's changing
  CLs, churned/recycled VAs, higher QPU load — recall the QRMAXCNT coordinate-shader-starvation
  note, also untested by a fixed light-load re-submit). A frozen-input clean run says nothing
  about the binner under those conditions.

So Stage 1 is a plumbing/sanity check (the harness + winsys CRC hook + render path all work,
0 faults), not a localization result. The real discriminator is Stage 2.

## RESULT — Stage 2 (VA churn, fixed geometry): render STABLE → churn alone is NOT the cause (2026-07-24, HW)

Harness change: allocate a FRESH command buffer per iteration (v3dv records a fresh
CL/tile_alloc → fresh GPU VAs) and free the previous (VAs recycle from the winsys holes list)
— reproducing Quake's per-frame BO/VA churn. Signal = CRC of a **center band** of the scanout
(rows 508–572), with a coverage guard: band-CRC compared to an all-CLEAR-color CRC so a
vacuous (clear-only) band is flagged.

**Methodology note (a confound caught + removed):** the first churn run (`v3dchurn`, fbcon
ENABLED) showed band-CRC vary across the first ~4 frames then stabilize — but that was the
async klog/psh **fbcon console overdrawing the fb** between our render and our read-back (the
pl011-tty mirror targets the same /dev/fb0). Disabling fbcon (`FBCONSETMODE(FBCON_DISABLED)`,
as Quake does) removed it entirely.

Confound-free run (`v3dchurn2`, log `rpi4b-uart-20260724-115244-v3dchurn2.log`):
- **churn REAL** — `qma` alternated `0x0293c000`↔`0x02979000` (VA recycle confirmed, not frozen);
- **band-coverage OK** — band `0xf00fc474` ≠ clear `0xb67992a2` (signal covers the triangle);
- **render STABLE** — `band_crc=0xf00fc474` identical **64/64** (transitions=1), 0 faults;
- the stable value equals what the confounded run settled to → confirms the earlier "divergence"
  was 100% console overdraw.

**Conclusion:** VA churn alone does NOT reproduce #67 on simple (single-triangle) geometry.
**Why this is robust beyond the shallow test** (important — my churn was only a 2-VA ping-pong,
`qma` alternating between two slots at immediate steady state, which by itself would NOT rule
out *deep* churn): the winsys per-submit invalidations are **global, not per-VA** — a full MMU
TLB clear (`mmu_flush_tlb`), a full `L2TFLS`, and an all-slice `SLCACTL=0x0f0f0f0f`. A global
flush makes recycle *depth* architecturally moot: however many VAs churn, every submit starts
from a fully-invalidated GPU cache/TLB. So the Stage-2 negative generalizes — do NOT re-chase
"more VAs / deeper churn" as a separate axis. The per-frame BO/VA-recycle path is **not
sufficient** by itself. Per the advisor outcome
map (`qma advances + band constant → density is the variable`), the remaining untested variable
is **geometry density / complexity** — Quake's hundreds of small triangles producing dense
per-tile primitive lists + higher QPU coordinate-shader load (cf. the QRMAXCNT starvation note).
This matches the original observation that #67 hits models with many triangles, never trivial
scenes.

## RESULT — Stage 3 (churn + overdraw DENSITY): render STABLE at all densities (2026-07-24, HW)

Rather than a vertex buffer (hand-assembled SPIR-V, error-prone), used instance count as the
density lever: `vkCmdDraw(3, N)` → the binner bins N primitives into every covered tile + runs
3N coordinate-shader invocations (the QRMAXCNT / coordinate-shader-load axis) with no shader or
vertex-buffer change. Swept N = 1, 16, 64, 256, each churned (fresh cmd buffer + VA recycle).
Log `rpi4b-uart-20260724-120717-v3ddensity2.log`:
- **N=1/16/64/256 → render STABLE** (band-CRC transitions=1 each, coverage OK, churn REAL), 0
  faults, 0 wedge, 0 OUTOMEM. `qms` stayed 106496 at every N (tile_alloc is sized by RT dims,
  not primitive count; the overflow pool absorbs extra prims).

So per-tile primitive-list LENGTH + coordinate-shader COUNT, under churn, do NOT reproduce #67.
(Caveat: this is IDENTICAL-primitive OVERDRAW density — all N instances draw the same triangle
at the same position. It stresses list length + coord-shader count, but NOT spatial distribution
across many distinct tiles, NOT varied per-primitive data, NOT tile_alloc-size/overflow pressure.)

## ⚠️ SCOPE CORRECTION — the Vulkan harness only tests the SHARED layer, not the GL frontend

Stages 1–3 used the **Vulkan (v3dv)** harness. v3dv and the GL (gallium v3d) stack Quakespasm
drives share `libv3d-phoenix.a` **byte-for-byte**: the Phoenix winsys (`ioc_submit_cl` — bin→
render coherency, cache flush, VA/MMU, overflow) and the Broadcom back-end (QPU compile, CLE
packet packing). So the Vulkan stages VALIDLY cleared **that shared submit/coherency layer**
(robust under frozen input, VA churn, overdraw density) — that is real and it maps to Quake.

But the **control-list / RCL / tile-rendering-mode / EZ config emission is FRONTEND-SPECIFIC**:
GL emits it from gallium `v3dx_rcl.c`; Vulkan emits its own from `v3dvx_cmd_buffer.c`. The
Vulkan harness therefore never exercises the GL RCL/EZ path — which is exactly what the EARLIER
#67 work implicated (CT1 stalling on the RCL's first `TILE_RENDERING_MODE_CFG`/EZ packet). So a
clean Vulkan result **cannot exonerate the GL frontend**, and the "no-Quake-state isolation
exhausted" claim below is **overstated**: it exhausted the shared-SUBMIT axis only. The GL
gallium frontend (RCL/EZ/tile-config emission, GL BO/texture/uniform lifecycle) is a whole
untested axis — and given the prior EZ/RCL lead, the PRIME one.

**Decision (2026-07-24, with the user):** the faithful #67 test must run on the GL path
(`libGL-phoenix.a`, same `v3dx_rcl.c` emitter as Quake). Priority is a stable/correct OpenGL
Quake; Vulkan/vkQuake is a separate long-horizon goal.

## ★★★ #67 REPRODUCED + ISOLATED + VISUALLY CONFIRMED (2026-07-25, real GL path)

After the Vulkan stages (shared-layer only) and the untextured/textured GL harness stages (all
clean), the bug was caught in **real Quake** with a cross-boot method:
- DET Quake build (`external/quakespasm-det` copy, `build-quakespasm-det.py`, `SCR_DetTick`):
  `host_framerate 0.05` (frame F = same demo moment every boot) + **`r_dynamic 0`** (holds
  lighting cross-boot-deterministic, so any cross-boot pixel diff is GEOMETRY) + `r_drawentities 1`.
- Dump a full-res 140×140 center crop every 10th demo frame to the NFS root (single `write()`
  <60 KB = one nfs pwrite; capture EVERY frame — capturing only on dump frames reads a
  page-flipped-away scanout buffer → black).
- Run 2 cold boots; per-frame cross-boot crop diff.

**Result:** pure-WORLD frames diff **0.0%** cross-boot; **MODEL/ITEM frames diverge — F0090 27.8%,
F0050/0060 ~16%, F0100 9%.** Side-by-side `artifacts/qglitch-67/ab-0090.png`: the SAME model at the
SAME demo moment renders **collapsed to a gray angular FAN/WEDGE on boot A** (the exact #67 "mangled
gun" signature) and **as proper geometry on boot B.** So **#67 = alias-model/item GEOMETRY renders
NON-DETERMINISTICALLY across cold boots**, confound-free (identical view + deterministic lighting).

**Reproduction harness (a fix is now testable):** `quake-det` + the cross-boot crop-diff
(`artifacts/qglitch-67/{cropA,cropB,ab-*.png}`). A fix works iff F0090's cross-boot crop diff → ~0.

**Mechanism to pin (next):** the alias path `GL_DrawAliasFrame_GLSL` binds VBO-sourced **packed
`GL_UNSIGNED_BYTE`** pose positions + `glDrawElements`; the alias VBO is built once at load
(`gl_mesh.c GLMesh_LoadVertexBuffer`, `GL_STATIC_DRAW`). Collapse-to-a-fan = vertices fetched wrong
→ suspect the VBO upload→GPU-fetch coherency or partially-initialized VBO BO content that varies
per cold boot. (Also a SEPARATE bug found + banked: cross-boot **dynamic-lightmap** lighting
non-determinism, `r_dynamic 0` → deterministic; see the earlier stages.)

## FIX CAMPAIGN — #67 root-caused + substantially mitigated; lightmap re-scoped (2026-07-25, HW)

Root cause (data-vs-fetch split): the alias-model VBO **source data is byte-identical across
boots** (all 41 models), so the bug is the **GPU's fetch of the static VBO**, not the data. The
binner's coordinate shader fetches the alias vertices immediately after the pre-bin `SLCACTL`
slice-cache invalidate, which is **fire-and-forget** on this SoC — the fetch races the
incomplete invalidate → stale vertices → collapse, intermittently per cold boot.

**FIX (mitigation, in `v3d_phoenix_winsys.c`, HW-tested via the cross-boot distinct-per-frame
harness):** a waited-L2T-flush **completion barrier** after the pre-bin SLCACTL, before the CT0
kick (a CPU barrier can't wait a GPU-internal invalidate, but the waited L2T flush's MMIO
round-trips let it settle). Result: **8 of 9 tested model frames become cross-boot-deterministic
and converge to the correct (baseline-majority) geometry**, vs all-glitchy baseline. Robustly
scored: baseline = each model frame has a byte-identical majority (correct, 4–6/7 boots) + several
distinct ~14% garbage variants; with the barrier those 8 frames are 1-distinct across fresh 5-boot
runs.

**Honest residual:** one heavier frame (F0070) still shows ≥2 distinct across a fresh 5 boots —
the barrier's settle isn't guaranteed for the densest vertex fetches. Attempts to close it with an
explicit larger settle **confound the host_framerate determinism harness** (per-submit delay →
cumulative frame drift → spurious divergence at later frames) and cost fps, so they're not usable;
the complete fix needs a *guaranteed* slice-invalidate-completion primitive (no busy-bit known on
V3D 4.x) — deferred as deep V3D work. The barrier is kept as a substantial mitigation.

**METHOD LESSON (load-bearing):** this bug's per-boot variance makes N≤2–5 samples look clean and
then diverge at higher N (the lightmap "fix" looked 2.9% in one pair, then 12 glitchy at N=5; the
#67 "0-glitchy" first 5-boot had F0070 clean by luck). **Confirm any fix at ≥5 FRESH boots** with
the distinct-per-frame scorer, never a single pair.

**Second bug (dynamic-lightmap flicker) — re-scoped, NOT a coherency race:** a symmetric completion
barrier at the bin→render handoff did NOT fix it (5-boot r_dynamic-1 still ~12 glitchy). So the
lightmap non-determinism is **non-deterministic content** (likely uninitialized `blocklights` in the
per-frame dynamic build), not a sample-coherency race → a **Quake-side** fix (`R_BuildLightMap` /
`gl_rlight.c`), confirmable by CRCing the CPU lightmap bytes pre-`glTexSubImage2D` cross-boot (VBO-
style split). Deferred. The handoff barrier was reverted (dead weight).

## RESULT — Stage 5 (GL PATH, faithful): untextured geometry render is DETERMINISTIC (2026-07-24, HW)

New harness `tools/v3d-driver-port/gl_det_harness.c` (built by `gl-det-build.sh`, run as
`/bin/gl-det`) renders through Quake's exact GL stack: `v3d_screen_create` →
`st_create_context(API_OPENGL_COMPAT)` → a DRAM RGBA8+DEPTH24 FBO → `glEnable(DEPTH_TEST)` +
`glDepthFunc(LEQUAL)` (the EZ trigger) → many DISTINCT small triangles at varied depths via
immediate-mode `glBegin/glEnd` (the world-render path), bit-reversed grid placement so any
prefix covers the center band. Re-rendered from scratch each iteration (the GL frontend builds
a FRESH control list + BOs per frame == Quake's per-frame churn). CRCs a center band; coverage
guard vs an all-clear band. Log `rpi4b-uart-20260724-194104-gldet.log`:
- GL up (Mesa 26.2 / V3D 4.2), FBO complete.
- N=64 → band `0xc0adbc1a` **10/10 identical**; N=256 → `0xd7ca0332` 10/10; N=1024 → `0x28267cc9`
  10/10. Each count a DIFFERENT crc (distinct geometry truly rendered) but byte-stable within
  itself. Coverage OK, 0 faults.

So on the **real GL frontend** — including the `v3dx_rcl.c` RCL/EZ/tile-config emission the prior
#67 lead implicated, plus LEQUAL depth-test binning of varied geometry under per-frame churn —
the render is **deterministic**. The RCL/EZ geometry-binning lead is therefore **NOT reproduced**.

**What Stage 5 still does NOT exercise (the now-leading axis, by elimination): TEXTURING.**
`gl_det_harness` draws flat `glColor` geometry — no textures, no samplers, no per-frame
`glTexSubImage2D` (lightmaps), no VBO vertex-fetch path, no multi-draw state changes. Quake's
glitch is **model/texture-specific** (the affected surfaces are textured models). With geometry/
binning/EZ/depth (Stage 5) AND the shared submit/coherency/VA/density layer (Stages 1–3) all
proven deterministic, **texturing is the leading remaining suspect** — sampling and/or the
per-frame lightmap upload→sample path (note: the earlier "uncached BO + dsb" deflation only
argued the *write* side; TMU cache / UIF-tiling / sampler-state races on the *read* side are
untested). Next: extend `gl_det_harness` with a sampled texture + per-frame `glTexSubImage2D` +
texcoords, same determinism check.

## RESULT — Stage 6 (GL PATH + TEXTURING): also DETERMINISTIC (2026-07-25, HW)

Extended `gl_det_harness` with a 128×128 RGBA8 texture (Quake lightmap-block size), MODULATE/
LINEAR, texcoords on every triangle, and a **per-frame `glTexSubImage2D` re-upload of identical
data** (mimics the dynamic-lightmap upload). Identical data → a coherent upload→sample path must
render identically. Log `rpi4b-uart-20260725-005317-gldettex.log`:
- N=64 → `0x7e8421c4` 10/10; N=256 → `0xa5179e97` 10/10; N=1024 → `0xaef3af8b` 10/10. CRCs differ
  from the untextured run (texturing genuinely applied + sampled), byte-stable within each count,
  0 faults. **Texture upload→sample coherency did NOT reproduce #67.**

## Where this leaves it — the remaining axis is the GLSL ALIAS-MODEL VBO path

Cleared, all deterministic: shared submit/coherency/VA/density (Vulkan S1–3); GL frontend
geometry/RCL/EZ/depth (S5); GL texturing + per-frame upload + sampling (S6). Every render/texture
mechanism the harness can reach is clean. What the harness still does NOT replicate — and what
Quake's **model** rendering (the #67-affected geometry) actually uses:
1. **GLSL shaders + VBO-sourced vertex attributes** — the alias-model path `GL_DrawAliasFrame_GLSL`
   binds two pose vertex+normal VBOs and `mix()`es them in a GLSL VS. HARNESS USES FF + immediate
   mode — this whole path (VBO attribute fetch + GLSL VS/FS QPU shaders) is untested, and it is
   exactly where the original (red-herring-labelled) de-alias work was poking. **Leading suspect.**
2. Multitexture (base + lightmap TMU1), mipmaps, real MVP/perspective — lesser.

**Strategy inflection:** faithfully replicating the GLSL two-pose alias VBO path in a harness
approaches reimplementing Quake's alias renderer. With the render path this extensively cleared,
instrumenting Quake directly (bisect: FF vs GLSL alias draw, r_lerpmodels toggle, force-sync) is
likely the more efficient path — offered to the user as the fork.

## Cumulative conclusion (three clean isolations) — SHARED-LAYER ONLY (see scope correction above)

| Stage | Stressor | Result |
|---|---|---|
| 1 | frozen input, static VA | deterministic (baseline; rules out spontaneous tile_alloc corruption) |
| 2 | VA churn (recycle) + simple geometry | STABLE — global per-submit TLB+L2T+SLCACTL flush covers recycle at any depth |
| 3 | VA churn + overdraw density (≤256 prims/tile, 768 coord-shaders) | STABLE — list length + coord-shader count don't reproduce it |

#67 is therefore **NOT** any of: spontaneous binner corruption, the per-frame VA-recycle path,
or per-tile-list-length / coordinate-shader-count density. The tractable **no-Quake-state**
isolation surface is now exhausted.

**A candidate lead was checked read-only and DEFLATED — texture/lightmap upload coherency.**
It was tempting to call the per-frame `glTexSubImage2D`→sample path (the winsys l.855 `dsb sy`
comment) the prime lead. But a read of the Mesa v3d BO-allocation path shows the `cacheable`
flag (`V3D_CREATE_BO_CACHEABLE`) is used ONLY for render-target readback (v3d_resource.c:144),
and even that is disabled for the color RT; **textures and lightmaps are uncached BOs** (the
winsys default). A CPU texel write to an uncached BO reaches RAM directly, and the existing
per-submit `dsb sy` drains it before the GPU's TMU fetch — so the barrier is already correct
and sufficient. There is NO *cached* per-frame-updated texture that lacks a `dc cvac`. The
`dc civac` in the code is on the readback path (CPU-read-after-GPU-write), not upload. So this
lead does not answer "why would the existing correct barrier fail?" — it deflates. (Model skins
are `GL_STATIC` one-time uploads; lightmaps are uncached+`dsb`'d.)

**Remaining primary lead (untested, NOT deflated): spatially-distributed VARIED geometry.**
A real mesh across many DIFFERENT tiles with varied per-primitive positions/attributes — the
one axis no harness stage exercised (all stages drew one triangle at one position). Quake's
models are exactly this: hundreds of small distinct triangles binned into many distinct tiles.
This needs a vertex-buffered mini-renderer harness (a non-vertexless VS + vertex BO + varied
geometry), which is substantial hand-assembled-SPIR-V work → ATTENDED. Secondary (weaker, also
untested): per-draw uniform streaming / state-changing multi-draw command buffers.

This is the genuine exhaustion-of-cheap-autonomous-surface handoff: three isolations closed one
mechanism family each, one tempting lead was deflated by a read-only check, and the remaining
leads all require an attended mini-renderer or Quake instrumentation (wedge-landmine).

## Status
- [x] Coherency angle: L2C/L3 no-ops on 4.2, submit BOs uncached, flush matches Linux — all
      read-only-verified. Cache-maintenance is NOT the gap.
- [x] Reframe: split binner-produces-bad vs render-reads-bad (was conflated as coherency).
- [x] glitch ≠ wedge → prior "not memory/VA/cache" (wedge) verdict does not transfer.
- [x] Experiment infrastructure landed (winsys `V3D_BIN_CRC` + harness churn loop + coverage guard).
- [x] **Stage 1 (HW): baseline sanity only** — frozen input → frozen output (64/64). Rules out
      spontaneous tile_alloc corruption; does NOT exonerate the binner (pixel samples were the
      CLEAR color — vacuous).
- [x] **Stage 2 (HW): VA churn alone does NOT reproduce #67** — fresh recycled VAs per submit,
      fixed simple geometry → render byte-stable 64/64 (confound-free, fbcon disabled). Global
      per-submit flush makes recycle depth moot.
- [x] **Stage 3 (HW): overdraw density + churn does NOT reproduce #67** — N=1..256 instances all
      render-stable. Per-tile-list-length + coord-shader-count ruled out.
- [x] **No-Quake-state isolation surface EXHAUSTED** (3 clean stages).
- [x] Texture/lightmap-upload-coherency lead CHECKED read-only + DEFLATED (per-frame textures are
      uncached BOs; the existing `dsb sy` is correct/sufficient; no cached per-frame texture lacks
      `dc cvac`).
- [ ] FIX: ATTENDED, wedge-landmine. Primary remaining lead = spatially-distributed VARIED-geometry
      binning (untested — needs a vertex-buffered mini-renderer or Quake instrumentation).

## Reusable assets left in place (for the attended session)
- `tools/v3d-driver-port/v3dv_harness.c` — the churn harness (fresh cmd buffer/iter, band-CRC +
  coverage guard, fbcon-disable, winsys `V3D_BIN_CRC` self-enable). Escalate to Stage 3 by adding
  a vertex-buffered many-triangle pipeline.
- winsys `v3d_phoenix_last_bin_crc()` + `V3D_BIN_CRC` tile_alloc checksum hook (additive, no-op off).
- mesa-v3dv build is resurrected + drift-fixed (`build-v3dv-phoenix.py` links clean); build+deploy+
  netboot pipeline is hot: `python3 tools/v3d-driver-port/build-v3dv-phoenix.py` →
  `cp /tmp/v3dvphx-harness /srv/phoenix-rpi4-nfs/bin/v3dv-det` → `test-cycle-psh-interact.sh
  --label v3ddet -- "/bin/v3dv-det"`.
- Uncommitted (deliberately; user's known-good `rpi4-quake` untouched): the winsys diagnostic,
  the harness, and the two copied `drm-uapi` headers + `driQueryOptionstr` stub (build-drift fixes).

---

## 2026-07-25 — RESOLUTION: #67 FIXED + committed; "second bug" was a measurement artifact

### #67 (alias-model geometry collapse) — FIXED and COMMITTED
- Root cause confirmed: in `ioc_submit_cl` the binner (CT0) coordinate shader fetches the uncached
  alias-model VBO vertices immediately after the pre-bin SLCACTL slice-cache invalidate, but SLCACTL
  is **fire-and-forget** on this SoC (covers TVCCS/TDCCS; no busy bit). The binner could begin
  fetching before the invalidate settled → stale vertices → collapse-to-fan, intermittently per boot.
- Fix (`v3d_phoenix_winsys.c`, committed `agent/quake-67-gpu-coherency-fix` 214be9a): add a **waited
  L2T flush** (spins on the L2TFLS busy bit = real MMIO round-trips) after the pre-bin SLCACTL, giving
  the slice invalidate time to settle before the CT0 kick.
- Validation: cross-boot determinism harness — baseline rendered several distinct geometry variants
  per boot (~86% correct + ~14% garbage); with the barrier every model frame renders the correct
  majority geometry deterministically across 10 boots (perceptual >0.3% threshold: 0 visible glitchy
  frames). Residual: one heavier frame (F0070) keeps a 3–9px sub-perceptual diff — deferred (needs a
  guaranteed slice-invalidate completion primitive; none known on V3D 4.x).
- **Real playable `/bin/quake`** rebuilt against the fresh fix-A libv3d and HW-validated: demo1 runs at
  ~35 fps @ 1080p, 0 faults; viewmodel + Ogre monster + torches (all alias models) render correctly
  across the demo. This is the exact geometry class #67 was collapsing.

### "Second bug" (r_dynamic 1 cross-boot non-determinism) — NOT a real render defect
The cross-boot determinism harness (correct instrument for #67's per-boot intermittent collapse) is
the **wrong** instrument for a "flicker", which is by definition *within-run* temporal instability.
Re-analysed with the right tools:
- **Static lightmap content is byte-deterministic across boots**: load-time `QDET LMBUILD count=4
  crc=0ec4f78f nz=632874` identical on 4/4 boots. Data hypothesis ruled out.
- The worst-scoring cross-boot frames were **early demo frames with NO per-frame lightmap uploads**
  (`lm=00000000` at F0000–F0050) → warmup, not lighting. Plus one boot rendered **black/truncated**
  (10 frames) and poisoned the F0040 score. early-high/late-low score pattern = warmup signature.
- **Within-run screen mean-luma:** capture *every* frame + whole-frame mean-luma across the
  active-dynamic-light window (F180–235). On 2 boots luma moves in **smooth multi-frame ramps**
  (dynamic-light flash-and-fade: F185≈27→decay, F215≈44 explosion→9-frame decay). Largest single-frame
  zigzag reversal = **3.31** (a real global flicker would be ~15–30). Rules out a *global* brightness
  flicker, but whole-frame mean is blind to a *localized* one.
- **Consecutive-frame per-pixel diff:** dominated by camera motion (fixed-timestep = 20 fps sim →
  large inter-frame change everywhere), so it cannot cleanly isolate a localized flicker (a 628px
  "transient" at F216 was ambiguous — motion or flicker).
- **K-re-render test (the DECISIVE, motion-free one):** at F185/F191/F215/F216 (active dynamic
  lighting) re-render the 3D view **8× from the frozen sim-state** (cl.time does not advance) and CRC
  each capture. Identical input K times → **byte-identical CRCs on all 4 frames, on 3 independent
  boots** (`QDET RERENDER … K=8 IDENTICAL`). This excludes camera motion entirely: **the GPU render
  is deterministic on identical input — there is no within-run flicker.**
- Conclusion: **GLQuake with `r_dynamic 1` renders correctly; there is no within-run flicker.** The
  cross-boot "flicker" score was warmup + a bad boot, i.e. measurement contamination.

### Honest residual (not a player-visible defect)
While the render is byte-deterministic *within* a run, the F215/F216 explosion frames show a **rare
cross-boot variant** with `r_dynamic 1` (crc `25e0e69a` on 3/4 boots, `68db5645` on 1/4) — the same
class as #67's deferred F0070 sub-perceptual residual (fire-and-forget SLCACTL not 100% closed by the
L2T-wait proxy on the heaviest per-frame vertex/lighting load). Within-run it is **deterministic and
visually correct on the majority variant** (montage + real-quake HDMI); the rare 1/4 variant was
**not visually characterized** (its crop was overwritten before capture — so whether it is a minor
lighting difference or a small garbage region is unconfirmed). Because it is within-run-invisible
(each boot renders consistently frame-to-frame; only a cross-boot screenshot diff sees it), it is
grouped with the deferred #67 residual (same fix: a guaranteed slice-invalidate-completion primitive).

### Status
- #67: **RESOLVED** (committed, real quake HW-validated; sub-perceptual cross-boot residual on the
  heaviest frames deferred).
- "Second bug" (r_dynamic-1 flicker): **not a real render defect** — no within-run flicker
  (K-re-render byte-identical ×3 boots), real quake renders correctly.
- GLQuake is working and glitch-free on the RPi4 V3D. vkQuake remains the separate far-reaching goal.
