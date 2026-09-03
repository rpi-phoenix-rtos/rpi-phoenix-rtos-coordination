# vkQuake start-map torches: why the flame alias models produce ZERO pixels

Date: 2026-09-03
Scope: **static analysis only.** No build, no Pi cycle, no writes under `external/`.
Companion to: [`2026-09-03-quake-torch-regression-archaeology.md`](2026-09-03-quake-torch-regression-archaeology.md)
(read that first — it establishes that `d3e329c` ships intact in fork, patch, SPIR-V and ELF
while the torches stay absent).

Measured starting point (not inferred):

- `./scripts/check-torch-rois.py --label qs-mapstart2` → quakespasm **401–468** / **417–548** lit px in the two archway torch ROIs.
- `./scripts/check-torch-rois.py --label vkq-mapstart` → vkQuake **0 and 0** across 7 frames at the same viewpoint (viewpoint match confirmed, mae 3.4–3.6).

---

## 0. Answers up front

### 0.1 Does V3DV expose `fillModeNonSolid`? Yes — but the port throws it away.

- **V3DV advertises it:** `external/mesa/src/broadcom/vulkan/v3dv_device.c:334` → `.fillModeNonSolid = true`.
- **The shim even enables it on the device:** `sources/phoenix-rtos-ports/vkquake/glue/pl_phoenix_vk_vid.c:872`
  copies `device_features.fillModeNonSolid` into `VkPhysicalDeviceFeatures enabled`.
- **Then the shim hardcodes the engine flag off:** `pl_phoenix_vk_vid.c:876` →
  `vulkan_globals.non_solid_fill = false;` (in the block that also forces `screen_effects_sops`,
  `ray_query` and `multi_draw_indirect` off).

Consequences:

- `R_ShowTris` early-returns: `external/vkquake/Quake/gl_rmain.c:763` (`… || !vulkan_globals.non_solid_fill`).
- `gl_rmisc.c:2904, 3191, 3604, 3740, 4176` gate the showtris/showbboxes-line pipeline creation on the
  same flag, so those pipelines were never even built.

**So the `+r_showtris 1` probe run earlier was INERT. It proves nothing about geometry.**
`r_showtris` is not a usable instrument on this port today. It can be re-armed by deleting the
one override line at `pl_phoenix_vk_vid.c:876` (V3DV supports the feature) — worth doing as a
follow-up, but it needs a rebuild, so it is not on the critical path below.

Related note for future probes: `r_showbboxes` is *partly* alive — `gl_rmain.c:695`
(`if (pass == 0 && !non_solid_fill) continue;`) skips only the wire pass, and pass 1 draws a
`Draw_String_3D` number per **server edict**. But `makestatic()` frees the QC edict, so the
walltorches have no edict and `r_showbboxes` cannot see them. Not useful here.

### 0.2 H1 (drawn but invisible via alpha) is REFUTED — six independent kills

1. **The premise of `d3e329c` is false in this codebase.** It claims "for a fullbright-containing
   skin the nobright *diffuse* is transparent exactly where the fullbright texels are". In vkQuake
   1.34's `TexMgr_LoadPalette` the nobright palette sets **alpha = 255 on every index**, blacking out
   only RGB for 224–255: `external/vkquake/Quake/gl_texmgr.c:573-588` (`dst[3] = 255;` at `:586`).
   Only the `*_fence` variants zero an alpha, and only for index 255 (`:590-596`).
2. **flame.mdl contains zero index-255 texels** (measured from `pak0.pak`: skin 296×140, 41 440
   texels, `index255 = 0`, 23.3 % fullbright). So even the fence path cannot produce a transparent
   texel, and `TexMgr_LoadImage8`'s "detect false alpha cases" (`gl_texmgr.c:1330-1336`) would strip
   `TEXPREF_ALPHA` anyway.
3. **`ubo->flags |= 0x10` (r_alias.c:201-206) and `result.a = 1.0` (Shaders/alias_common.inc:28-29)
   are a NO-OP for this model.** `result.a` is already `original_diffuse_tex_a * entalpha` = `1.0 * 1.0`
   (`alias_common.inc:22`). `d3e329c` overwrote 1.0 with 1.0.
4. **lavaball.mdl is 88.2 % fullbright — nearly 4× flame's 23.3 % — and it renders.** It is the red
   faceted model in `artifacts/hdmi/*-vkq-mapstart-*.png` (verified by zoom: shaded polygonal facets,
   distinct from the square particle sprites in its trail). Under H1 the lavaball would be the first
   casualty. It is not a casualty.
5. **Alias pipeline 0 has `blendEnable = VK_FALSE`** (`gl_rmisc.c:3546`, `pipeline_index & 2 == 0`) and
   `depthWriteEnable = VK_TRUE`. A rasterized fragment therefore **writes colour unconditionally**;
   alpha cannot hide it in the colour buffer. And the *fb0 scanout* story cannot explain the frame
   either: a dropped-alpha pixel would show black / the clear colour, not a **perfectly continuous
   wall texture**. Pixel-level crop (`quakespasm` vs `vkQuake`, same ROI, ×3 brightness) shows the
   vkQuake ROI carrying the unbroken brick-and-rivet pattern, with **no dark silhouette**.
6. (Bonus.) **The torch *holder* is missing too.** In the quakespasm frame the torch is flame + a dark
   non-fullbright bracket below it. Both are gone in vkQuake. An alpha effect keyed on fullbright
   texels cannot remove the non-fullbright half of the same model.

**Corollary — the headline for the archaeology doc's open ambiguity:** `d3e329c` was a **no-op from
day one**. `has_alpha` is false for this model (`TEXPREF_ALPHAPIXELS` is only ever set inside
`TexMgr_LoadImage32`'s `glt->source_format == SRC_RGBA` branch, `gl_texmgr.c:976-990`, and an MDL skin
stays `SRC_INDEXED` through `TexMgr_LoadImage8`, `gl_texmgr.c:1313-1375`), so the flag path fires
exactly as designed — and changes nothing. The 2026-08-03 diagnosis was wrong at the premise, so
"the fix never worked and both closures were misreads" is now the *established* reading, not the
merely-parsimonious one. There is no need to hypothesise a V3D/Mesa regression between 2026-08-04
and 2026-08-22.

### 0.3 What the pixels do say

Combining kill #5 and #6: **the flame draws contribute nothing to the colour buffer at all.** The
question is no longer "why are the pixels dark" but "why are there no fragments".

---

## 1. New measurement this session: the torch ROI is frame-INVARIANT

`flame.mdl` frame 0 is a **group of 6 poses** with `interval = 0.1` (measured from the pak), so
`R_SetupAliasFrame` cycles `posenum` at 10 Hz (`r_alias.c:336-339`). Across the 7 gradeable
`vkq-mapstart` HDMI ticks (091118 → 091253, ~2.5 minutes of wall clock, and the lavaball/particles
demonstrably move between them, so `cl.time` is advancing) the two torch ROIs are statistically
**identical to 0.1 of a grey level**:

| frame | mae | L: lit / maxR / meanR | R: lit / maxR / meanR |
| --- | --- | --- | --- |
| 091118 | 3.53 | 0 / 63 / 31.5 | 0 / 74 / 32.3 |
| 091135 | 3.46 | 0 / 63 / 31.5 | 0 / 74 / 32.3 |
| 091152 | 3.43 | 0 / 63 / 31.5 | 0 / 74 / 32.3 |
| 091209 | 3.62 | 0 / 63 / 31.5 | 0 / 74 / 32.3 |
| 091226 | 3.59 | 0 / 63 / 31.5 | 0 / 74 / 32.3 |
| 091243 | 3.52 | 0 / 63 / 31.5 | 0 / 74 / 32.3 |
| 091253 (final) | 3.64 | 0 / 63 / 31.5 | 0 / 74 / 32.3 |
| quakespasm ref | 3.55 | **468** / 255 / 37.8 | **545** / 255 / 39.1 |
| 091015/091031/091047/091102 | 21–132 | — (pre-map: white/black/loading, not gradeable) |

(Frames 091015–091102 are the boot/loading screens — `mae` 21–132 — and are correctly excluded by
the viewpoint gate.)

**Interpretation:** the flame contributes zero pixels in *every* pose, not in some poses. This is
important because it constrains the leading candidate (§2, R1): pose 0 binds at VBO offset 0, which
is exactly the configuration in which lavaball and the viewmodel *do* render. A pose-dependent
defect predicts torches visible in ~1/6 of frames; the probability of missing that in 7 independent
samples is (5/6)^7 ≈ **0.28**, so this weakens R1's "non-zero offset" variant without excluding it.

---

## 2. The comparator table — this carries the whole argument

Everything measured from `/srv/phoenix-rpi4-nfs-gcc16/usr/share/quake/id1/pak0.pak`
(md5 `5906e5998f…`) by replaying `GL_MakeAliasModelDisplayLists`' dedup (`gl_mesh.c:203-268`) and
`GLMesh_UploadBuffers`' layout (`gl_mesh.c:348-349`, `meshxyz_t` = 12 B, `meshst_t` = 8 B).

| model | in scene as | poses | `numverts_vbo` | VBO total | pose-1/2 bind offset | `vbostofs` | blend | renders? |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `progs/flame.mdl` (31× archway/wall torch) | **static** (`makestatic`, efrag) | **6** | 120 | **9 600 B (2.34 pages)** | **1 440·pose → 0…7 200** | 8 640 | **0** (MOD_NOLERP) | **NO** |
| `progs/flame2.mdl` (10× fire pit) | **static** | **17** (6+11) | 88 | **18 656 B (4.55 pages)** | **1 056·pose → 0…16 896** | 17 952 | **0** (MOD_NOLERP) | **NO** (open since 2026-08-22) |
| `progs/lavaball.mdl` (`misc_fireball`) | packet entity | 1 | 20 | 400 B | **0** | 240 | 0 | **YES** |
| `progs/v_shot.mdl` (viewmodel) | `cl.viewent` | 7 | 96 | 8 832 B (2.16 pages) | **0** (idle → frame 0 → pose 0) | 8 064 | 0 | **YES** |

Model header flags (from the pak): `flame.mdl` **flags = 0x0**, `flame2.mdl` **0x0**,
`lavaball.mdl` 0x1 (EF_ROCKET). So `MF_HOLEY` is **not** set on the flames → `alphatest = 0`
(`r_alias.c:556`), → `pipeline_index = 0` (`r_alias.c:128`), the opaque non-blended pipeline. Same
pipeline the lavaball and the viewmodel use, and that pipeline demonstrably works.

Discriminators eliminated by the table:

- **Pipeline / render-pass / descriptor-set family: ELIMINATED.** lavaball and v_shot use
  `alias_pipelines[variant][0]`, the *same* handle, in the *same* `SCBX_ENTITIES`/`SCBX_VIEW_MODEL`
  contexts wired to the same `frame_cb` (`pl_phoenix_vk_vid.c:790-808`). They render. Therefore
  pipeline 0 exists, is non-NULL, is render-pass-compatible, and produces pixels.
- **`MOD_FBRIGHTHACK`: ELIMINATED.** Set for flame/flame2/boss (`gl_model.c:3789-3790`) but
  **never read anywhere in vkQuake** (`grep -rn MOD_FBRIGHTHACK Quake/` → definition + set site only;
  quakespasm reads it at `r_alias.c:656`). It is a dead flag in this engine.
- **"Two pose attributes bound to the identical address": NOT sufficient on its own.** lavaball
  (`numposes == 1`) and v_shot (idle) also bind bindings 1 and 2 to the same buffer at the same
  offset 0 (`r_alias.c:214-217` with `blend == 0` from `r_alias.c:174-177`), and both render.
- **Non-zero vertex-buffer bind offset per se: NOT sufficient.** v_shot binds its ST attribute at
  offset 8 064 (past the second page) and renders correctly textured.
- **Entity scale / transform defaults: ELIMINATED.** `CL_ParseBaseline` always assigns
  `ent->baseline.scale = ENTSCALE_DEFAULT` (`cl_parse.c:1332`) and `CL_ParseStatic` copies baseline →
  netstate (`cl_parse.c:1501`), so `ENTSCALE_DECODE` is 1.0, not 0. Statics never carry
  `LERP_MOVESTEP`, so `R_GetEntityLerpedTransform` returns `e->origin` verbatim (`r_alias.c:445-451`).
- **Model bounds / group-frame loading: ELIMINATED as a divergence.** `Mod_LoadAliasGroup` is
  byte-for-byte the same logic in both engines (vkQuake `gl_model.c:3214-3256` vs quakespasm
  `gl_model.c:2709-2749`), and `Mod_CalcAliasBounds` (`gl_model.c:3632-3736`) sweeps **all** poses.
- **Dynamic-UBO ring exhaustion: ELIMINATED.** 256 KiB ring (`gl_rmisc.c:146`) and the shim does call
  `R_SwapDynamicBuffers()` per frame (`pl_phoenix_vk_vid.c:1127`).

What survives is a short list, and it is **confounded**: in `start.bsp` *every* `makestatic` entity
is a flame (entity-lump census: 31 `light_torch_small_walltorch` + 10 `light_flame_large_yellow`, and
nothing else static), so "static entity" and "flame model" cannot be separated by this scene alone.

---

## 3. Ranked candidates

### R1 — The unported #67 V3D alias vertex-fetch defect (blend==0 + multi-pose VBO)

**Mechanism.** `flame.mdl` / `flame2.mdl` are the only alias models in the scene that reach this
exact configuration: `MOD_NOLERP` (`gl_rmain.c:93-96` — the default `r_nolerp_list` literally begins
`"progs/flame.mdl,progs/flame2.mdl,…"`) forces `R_SetupAliasFrame` down its else branch
(`r_alias.c:341`), giving `pose1 == pose2` and hence `blend = 0` (`r_alias.c:174-177`), while the
model is **multi-pose**, so both position bindings land on the *same non-zero* offset
`numverts_vbo · pose · 12` inside a **multi-page** vertex buffer (`GLARB_GetXYZOffset`,
`r_alias.c:73-76`; bindings at `r_alias.c:214-217`).

This is precisely the pattern that needed **two separate fixes** on this exact GPU in the GL engine,
both of which vkQuake never received:

- `external/quakespasm 4ef0a42` — *"the V3D mishandles the alias draw when blend==0 … while BOTH byte
  position attributes Pose1Vert and Pose2Vert are enabled/fetched"* → don't bind Pose2 at `blend == 0`
  (`Quake/r_alias.c`, in-patch comment names #67).
- `external/quakespasm 3d742a3` — *"The V3D mis-fetches a single-pose alias VBO once it spans a second
  page (the wedge path reports `mmu_ill`), collapsing the geometry"* → shrink the VBO below 4 KiB
  (`vboposes = hdr->numposes`, `Quake/gl_mesh.c`).

vkQuake has no `blend == 0` special case anywhere in `GL_DrawAliasFrame` and no VBO-size mitigation.
`flame.mdl`'s VBO is 9 600 B (2.34 pages) and `flame2.mdl`'s is 18 656 B (4.55 pages).

**Why zero pixels rather than dim ones.** In GL the same defect produced *visible* mangled
black-triangle spikes. Under Vulkan/V3DV the collapse would corrupt the 16-bit UNORM position fetch
(`gl_mesh.c:440-448` writes `v·257`; `alias.vert:70-73` scales by 255), and a corrupted/NaN clip
position is discarded by the clipper rather than rasterized — no fragments, so the wall texture
survives untouched. That matches §0.3 exactly, whereas a shading bug does not.

**Honest caveat (state it, don't bury it).** Pose 0 binds at offset 0 — the working
lavaball/v_shot configuration — so a *pose-offset-dependent* defect predicts torches in ~1/6 of
frames, and §1 measured 0/7 with byte-identical ROIs. p ≈ 0.28 that pose 0 simply was never sampled.
So R1's "non-zero offset" variant is weakened; its "whole-buffer / attribute-record" variant (the
`3d742a3` family, where `attr.maximum_index = c_vb->size / stride` and the attribute address is
`bo + mem_offset + va.offset + vb.offset`, `external/mesa/src/broadcom/vulkan/v3dvx_cmd_buffer.c:2613-2655`)
survives intact.

**Decisive experiment — Run 1 below.** `r_lerpmodels 2` bypasses the `MOD_NOLERP` exclusion
(`r_alias.c:309` and `:341`: `!(e->model->flags & MOD_NOLERP && r_lerpmodels.value != 2)`), so the
flames get a **real two-pose lerp**: `pose1 != pose2`, `blend != 0`, two *distinct* offsets. If the
torches appear, R1 is confirmed **and `r_lerpmodels 2` is an immediate workaround**; the real fix is
the vkQuake analogue of `4ef0a42` (at `blend == 0`, bind binding 2 somewhere other than binding 1's
address, or add a single-pose pipeline whose vertex input omits bindings 2/3).

### R2 — The static entities never reach the alias draw list

**Mechanism.** Statics enter `cl_visedicts` only through the efrag path:
`CL_ParseStatic` → `R_AddEfrags` (`cl_parse.c:1513-1515`, `gl_refrag.c:170-200`) →
`R_StoreEfrags` from the visible-leaf sweep (`r_world.c:418` SIMD / `:929` scalar / `:617`
`R_StoreLeafEFrags` task) → `cl_visedicts[cl_numvisedicts++]` (`gl_refrag.c:253`). Packet entities
(lavaball) enter through a *completely different* site (`cl_main.c:893-897`) and the viewmodel
bypasses the list entirely (`R_DrawViewModel`, `gl_rmain.c:537-566`). So "everything that renders is
non-static; everything static is invisible" is a perfect, pose-independent, deterministic fit for the
observed data — including the separately-reported missing `flame2.mdl` fire-pit flames.

**Why zero pixels.** No draw is recorded at all.

**Honest caveat.** I traced this path end-to-end and **found no mechanism**: the efrag code is
shared lineage with quakespasm (which renders the torches), `R_StoreEfrags` correctly calls
`R_UpdateEntityAnimState` / `R_UpdateEntityMoveState` for statics (`gl_refrag.c:257-262`), the
`cl_numvisedicts < cl_maxvisedicts` guard cannot bite with 41 statics
(`cl_main.c:658-664` grows by 256 per frame), `R_DrawEntitiesOnList`'s non-task path iterates the
whole list from context 0 (`gl_rmain.c:1188` → `:463-530`), and `R_SortAlphaEntitiesTask` only skips
*opaque* entities from the *alpha* pass (`gl_rmain.c:883-885`). `R_CullModelForEntity`
(`gl_rmain.c:157-192`) uses the same 4-plane frustum that the world leaves pass. So: **fits the
signature best, no mechanism identified statically.** That is exactly why Run 1 carries a second
channel to measure it directly.

**Decisive measurement — the `Visedicts` / `Efrags` rows of the devstats overlay** (`+devstats 1`;
cvar registered at `Quake/host.c:95`; rows drawn at `gl_screen.c:740` and `:743`; counters fed at
`cl_main.c:980-984`). From the spawn point roughly 10–20 of the 41 statics should be in the PVS. A
reading of ~1–2 (just the lavaball ± tempents) confirms R2.

### R3 — Drawn, opaque, but lost (transform / depth / overwrite)

**Mechanism.** Three sub-cases: (a) the model matrix is wrong so all 41 flames land off-screen or at
the world origin (`R_RotateForEntity`, `gl_rmain.c:198-219`, then `TranslationMatrix(scale_origin)` +
`ScaleMatrix(paliashdr->scale)`, `r_alias.c:574-582`); (b) the flame fails the depth test against the
wall it hangs on (`depthTestEnable = TRUE`, `depthWriteEnable = TRUE` for pipeline 0); (c) it is drawn
and then overwritten by a later pass.

**Why zero pixels.** (a)/(b) both leave the wall pixel untouched.

**Evidence against.** I swept the frames for displaced geometry and found none that isn't accounted
for: the "speck column" on the right wall is the lavaball **plus its particle trail** (zoomed: a red
faceted model above square sprites), and the orange vertical strip on the left is a **wall texture
present in both engines**. Nothing torch-shaped appears anywhere in the vkQuake frame. (c) is
implausible because the viewmodel — recorded *after* the entities (`gl_rmain.c:1188-1192`) — survives.
Ranked last, kept for completeness.

### Explicitly refuted / eliminated (do not re-open)

| claim | verdict | evidence |
| --- | --- | --- |
| Fullbright texels have alpha ≈ 0 in the nobright diffuse | **FALSE** | `gl_texmgr.c:573-588` (alpha 255 for all indices) |
| `d3e329c`'s `flag 0x10` path isn't reached at runtime | **it IS reached — and is a no-op** | `has_alpha` false (`gl_texmgr.c:976-990` + `:1313`), `result.a` already 1.0 (`alias_common.inc:22`) |
| A separate fullbright/"nobright" pipeline or second additive pass overwrites alpha | **no such thing exists** | one draw, one pipeline; the fullbright add is in-shader (`alias_common.inc:19-20`) |
| Alpha-tested `discard` kills the flame fragments | **no** | `MF_HOLEY` unset (model flags 0x0) → `pipeline_index = 0`, `ALIAS_ALPHA_TEST 0` |
| Additive blending overwrites alpha | **no** | pipeline 0 `blendEnable = VK_FALSE` (`gl_rmisc.c:3546`) |
| WBOIT / OIT pass swallows the draw | **no** | `render_pass_index` is only MAIN or UI in this port (`pl_phoenix_vk_vid.c:797`) |
| Model load failure / bad group-frame parse | **no** | loader code identical to quakespasm; bounds swept over all poses |
| `MOD_FBRIGHTHACK` | **dead flag in vkQuake** | set at `gl_model.c:3790`, read nowhere |
| Entity scale 0 collapses the model | **no** | `cl_parse.c:1332` |
| `r_showtris` showed no geometry ⇒ no geometry | **probe was inert** | `pl_phoenix_vk_vid.c:876` + `gl_rmain.c:763` |

---

## 4. Experiments, in the order to run them

All three are **cvar-only — no rebuild, no source change** (the `cmdline` cvar fix landed as fork
`65f599e`, so `+cvar value` now takes effect on shareware data). Use the standard cycle with
`--capture-secs 240` / Bash `timeout: 420000`, then grade with `./scripts/check-torch-rois.py`.

### Run 1 (run this one) — splits R1 vs R2 in a single cycle

```
vkquake +devstats 1 +r_lerpmodels 2 +map start
```

Two independent channels, and the fixed torch ROIs stay valid because the viewpoint is unchanged:

| outcome | verdict |
| --- | --- |
| torch ROIs pass (`check-torch-rois.py` ≥ 8 lit px in both, ≥ 2 frames) | **R1 confirmed.** `r_lerpmodels 2` is an immediate workaround; fix = vkQuake analogue of quakespasm `4ef0a42`. |
| torches absent **and** `Visedicts` reads ~10+ | statics *do* reach the draw list → **R2 refuted**, R1's identical-binding variant refuted → go to Run 2. |
| torches absent **and** `Visedicts` reads ~1–2 | **R2 confirmed** → efrag/visedict path (`gl_refrag.c` / `r_world.c` leaf sweep). |

**Instrument self-check (do not repeat the showtris mistake).** The devstats overlay is
self-verifying: the `Edicts` row is always non-zero in a live single-player game. So
*overlay visible with `Edicts` > 0* means the channel is live and `Visedicts` is trustworthy;
*overlay missing entirely* means the channel is inert (the port's patch removed
`GL_SetCanvas`/`R_BindPipeline` from `SCR_DrawGUI`, patch hunk `gl_screen.c @@-1107`), **not**
`Visedicts == 0`. The HUD and crosshair render today, so `Draw_String` + `GL_SetCanvas` work and the
overlay is expected to appear (bottom-left, `gl_screen.c:723`).

### Run 2 (only if Run 1 is negative) — is the vertex-fetch-offset family alive at all?

```
vkquake +devstats 1 +chase_active 1 +map start
```

`chase_active 1` puts `cl.entities[cl.viewentity]` (`progs/player.mdl`) into `cl_visedicts`
(`cl_main.c:890`: `if (i == cl.viewentity && !chase_active.value) continue;`). player.mdl is
**317 verts_vbo × 143 poses = 546 508 B (133 pages)**, animating (`stand` cycles frames at 10 Hz →
`pose1 != pose2`, real lerp), so its position bindings sit at large, non-zero, deeply multi-page
offsets.

- player model renders → large non-zero multi-page pose offsets **and** the real two-pose lerp both
  work on V3DV → R1's remaining variant is refuted; the answer is in the static path or elsewhere.
- player model absent as well → the alias vertex-fetch path is broken far more broadly than the
  flames, and R1 is strongly supported.

**Warning:** chasecam moves the camera, so **the fixed torch ROIs and the mae viewpoint gate are
invalid for this run**. Grade it by eye ("is there a player model at screen centre"). That is why it
must not be Run 1.

### Run 3 (tiebreaker) — "never drawn" vs "drawn but lost"

```
vkquake +r_lightmap 1 +r_fullbright 1 +map start
```

`cl.maxclients == 1` in single player, so `r_lightmap_cheatsafe` becomes true (`gl_rmain.c:416-421`).
Then `R_DrawAliasModel` replaces the skin with `greytexture` (2×2 white), sets `fb = NULL`, forces
`entalpha = 1` and `lightcolor = (1,1,1)`, and sets `ubo->flags |= 0x2`
(`r_alias.c:593`, `:636-651`, `:196`) → the vertex shader emits `out_color = light_color`
(`alias.vert:74-79`) and the fragment is `white · 2.0`. **Any** produced fragment becomes solid white,
independent of every texture, palette, fullbright and alpha concern.

- a white torch-shaped blob appears in the ROI → the flame **is** drawn and rasterizes → R3
  (shading/present), and R1/R2 are both refuted.
- nothing appears → the flame produces no fragments → R1/R2.

Grade this one by eye or with a brightness-delta crop, not with `check-torch-rois.py` (its rule is
warm-biased: `R > G+20 && R > B+20`, which white fails), and note the whole scene goes grey.

---

## 5. If R1 is confirmed — shape of the fix (for the next session, not now)

`4ef0a42`'s GL trick (disable the attribute) has no direct Vulkan equivalent — a vertex binding is
part of pipeline state. Three options, cheapest first:

1. **Bind binding 2 to a different address at `blend == 0`.** At `blend == 0` the shader's
   `mix(p1, p2, 0)` ignores binding 2 entirely, so any in-buffer address works. E.g. bind pose2 to
   `GLARB_GetXYZOffset(hdr, (pose1 + 1) % hdr->numposes)`. One line in `r_alias.c:215-217`,
   `#if defined(__phoenix__)`-guarded. **But note this is *only* the `4ef0a42` half** — if the real
   trigger is the `3d742a3` (page-spanning buffer) half, this will not help, which is exactly what
   Run 1 distinguishes.
2. **A dedicated single-pose pipeline** whose `alias_vertex_input_state` declares only bindings 0/1,
   plus a shader variant that skips the `mix`. Correct, more invasive (a 5th `pipeline_index`).
3. **Fix it below vkQuake** in the V3DV/V3D attribute-record path
   (`v3dvx_cmd_buffer.c:2589-2660`) — the right long-term place if the defect is really the
   attribute address/`maximum_index` computation, and it would retire the two quakespasm band-aids too.

---

## 6. Bottom line, stated honestly

- **H1 (drawn but invisible via alpha) is excluded**, six ways, and `d3e329c` is confirmed as a no-op
  that could never have fixed anything. Do not re-apply or extend it.
- **The evidence does not yet single out R1 vs R2.** R1 has the strong prior (a proven, twice-fixed
  defect of exactly this shape on exactly this GPU, unported to vkQuake) but a weak fit to the
  frame-invariance measurement; R2 has the best fit to the signature but no mechanism found in
  source. Run 1 is designed to split precisely that pair in one cycle.
- Land the §5.1 torch-presence gate from the archaeology doc before signing anything off, so
  whatever Run 1 returns cannot be closed on a lavaball.
