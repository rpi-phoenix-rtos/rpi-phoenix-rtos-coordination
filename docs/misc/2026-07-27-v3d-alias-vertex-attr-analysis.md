# #67 GLQuake alias-model spike corruption — deterministic vertex-attribute analysis (2026-07-27, read-only)

Root-cause research for the **HW-confirmed deterministic** alias/MDL geometry corruption:
weapon pickups (grenade launcher spinning on the floor), the held viewmodel (sometimes), and
torch flames render with a subset of vertices flung into black degenerate-triangle spikes/wedges
while the rest of the model is roughly correct. Evidence: `artifacts/qglitch-67/2026-07-27-guns-still-broken/`.

**Scope note / relationship to prior docs.** The three prior #67 docs
(`2026-07-24-quake-glitch-coherency-localization.md`, `2026-07-26-gpu-torch-producer-analysis.md`,
`2026-07-26-gpu-linux-ordering-analysis.md`) chased a **boot-to-boot-varying coherency RACE**
(fire-and-forget `SLCACTL`; committed fix-A `214be9a`). The task states — and the current evidence
confirms — the surviving glitch is **DETERMINISTIC**, so that whole line is a wrong track for what
remains. I do not re-litigate it. One honesty check up front on why "deterministic" is the right
frame (stated as an assumption, not re-opened):

- fix-A (the race mitigation) is **already in this build**, yet the guns break **every frame** in
  the real-game video — not on a fraction of boots.
- The breakage is **pose-correlated** (see §3): it tracks exactly which models have `pose1==pose2`,
  not cold-boot timing. A race is boot-random and uncorrelated with pose. Therefore: structural.

---

## 0. Headline findings (TL;DR)

1. **The alias GLSL shader is correct** (§1). With `Blend==0` the position depends only on
   `Pose1Vert` and lighting only on `Pose1Normal`; byte data is finite so `mix(a,b,0.0)==a` exactly.
   The corruption is **below the shader**.
2. **The "two attributes aliasing the same address" theory is REFUTED** (§2, decisive). With the
   `gl_mesh.c` de-alias in place, a numposes==1 pickup binds `Pose1Vert`/`Pose2Vert` to **distinct
   addresses**, and with `blend==0` the position depends **only on `Pose1Vert`** — i.e. **no
   aliasing at all, `Pose2` mathematically unused** — yet the pickup **still spikes**. Aliasing
   cannot be the cause of a glitch that persists after aliasing is removed.
3. **The Mesa v3d byte-attribute translation is stock upstream and untouched by the Phoenix port**
   (§4). The port commit `1584b1a06d9` changes nothing in the `number_of_values_read_*` /
   attribute-record / VPM / VCM logic; that code is byte-for-byte the upstream Mesa that runs
   GLQuake glitch-free on Linux V3D 4.2 — including numposes==1 pickups with *real* aliasing. So the
   bug is **not** in the vertex-element→attribute-record translation.
4. **The real deterministic discriminator is `pose1==pose2` (⇒ `blend==0`, single effective
   pose)** (§3). Every broken class has it; every clean class lacks it. This — not aliasing — is the
   condition to design the fix and the HW experiment around.

---

## 1. Deliverable 1 — the alias GLSL shader, quoted, and verdict

Source: `external/quakespasm/Quake/r_alias.c:130-191` (`GLAlias_CreateShaders`). Vertex shader:

```glsl
#version 110
uniform float Blend;
uniform vec3 ShadeVector;
uniform vec4 LightColor;
attribute vec4 TexCoords; // only xy are used
attribute vec4 Pose1Vert;
attribute vec3 Pose1Normal;
attribute vec4 Pose2Vert;
attribute vec3 Pose2Normal;
varying float FogFragCoord;
float r_avertexnormal_dot(vec3 vertexnormal) {
        float dot = dot(vertexnormal, ShadeVector);
        if (dot < 0.0) return 1.0 + dot * (13.0 / 44.0);
        else           return 1.0 + dot;
}
void main() {
        gl_TexCoord[0] = TexCoords;
        vec4 lerpedVert = mix(vec4(Pose1Vert.xyz, 1.0), vec4(Pose2Vert.xyz, 1.0), Blend);
        gl_Position = gl_ModelViewProjectionMatrix * lerpedVert;
        FogFragCoord = gl_Position.w;
        float dot1 = r_avertexnormal_dot(Pose1Normal);
        float dot2 = r_avertexnormal_dot(Pose2Normal);
        gl_FrontColor = LightColor * vec4(vec3(mix(dot1, dot2, Blend)), 1.0);
}
```

(Fragment shader is a straightforward textured/fog/overbright/alpha-test path, `r_alias.c:164-191`;
it has no vertex-fetch relevance and is correct.)

**Verdict: the shader is correct, and it exonerates `Pose2` at `Blend==0`.**

- `mix(a, b, t) == a*(1.0-t) + b*t`. At `t==0.0`: `a*1.0 + b*0.0`. The pose bytes are `GL_UNSIGNED_BYTE`
  in `[0,255]` → always **finite**, so `b*0.0 == 0.0` (no NaN/Inf poisoning). `gl_Position` therefore
  depends **only on `Pose1Vert`** when `Blend==0`. Likewise `gl_FrontColor` collapses to `dot1`
  (only `Pose1Normal`). There is **no path** by which `Pose2Vert`/`Pose2Normal` reach the output at
  `Blend==0`.
- The compressed positions are used **raw** (0–255); the model's bbox scale/translate is applied
  via `gl_ModelViewProjectionMatrix` (`glScalef(paliashdr->scale)` / `glTranslatef(scale_origin)` in
  `R_DrawAliasModel`, `r_alias.c:693-695`) — standard QuakeSpasm. No missing scale in the shader.

So a correct rendering at `Blend==0` needs **only** a correct fetch of `Pose1Vert`. The corruption
is a fetch/geometry-processing defect below GLSL.

---

## 2. Deliverable 3 (headline) — the aliasing theory is REFUTED

The `r_alias.c:260-267` comment claims: *"The V3D port mishandles two vertex attributes aliasing the
same address → collapsed/garbage geometry (the classic pickup gun is a mangled wedge)"*, and
`gl_mesh.c:466-524` implements a de-alias: for `numposes==1` it allocates a **second, identical**
pose block so `GL_DrawAliasFrame_GLSL` can bind `Pose2` to a **distinct address** (`pose2bind =
(numposes==1)?1:lerpdata.pose2`, `r_alias.c:267`; block-1 fill `gl_mesh.c:500-524`). I verified the
de-alias is implemented correctly.

**Decisive trace for a numposes==1 pickup (grenade launcher on the floor):**

- `R_SetupAliasFrame` (`r_alias.c:494-501`): single-pose ⇒ `lerpdata.pose1 = lerpdata.pose2 = 0`.
- `GL_DrawAliasFrame_GLSL`: `lerpdata.pose1 == lerpdata.pose2` ⇒ **`blend = 0`** (`r_alias.c:248-255`).
- `pose2bind = 1` (de-alias). `GLARB_GetXYZOffset(hdr, 0)` (`r_alias.c:281`) → block 0;
  `GLARB_GetXYZOffset(hdr, pose2bind=1)` (`r_alias.c:282`) → block 1. **Distinct addresses,
  identical bytes.**
- Net: **`Pose1Vert` and `Pose2Vert` are NOT aliased**, and `blend==0` ⇒ **position = `Pose1Vert`
  only, `Pose2Vert` unused.**

The pickup nonetheless spikes (2026-07-27 video). A glitch that survives the complete removal of
aliasing **is not caused by aliasing**. QED. The `r_alias.c:260-266` / `gl_mesh.c:466-471` comments
are a **red herring**; the de-alias is harmless but not a fix.

(The task's "uncovered case" — a *paused animated* model, `numposes>1`, momentarily `pose1==pose2`
⇒ `pose2bind = lerpdata.pose2 = pose1` ⇒ still true aliasing — is real, but it is not the mechanism
either, because the *de-aliased* pickup with no aliasing at all still breaks. Both broken cases
share something else: §3.)

---

## 3. The actual discriminator — `pose1==pose2` ⇒ `blend==0` (single effective pose)

Mapping the observed broken vs clean classes to the draw state in `GL_DrawAliasFrame_GLSL`
(`r_alias.c:245-258`) and the frame setup (`R_SetupAliasFrame`, `r_alias.c:486-502`):

| Model class (evidence) | numposes | pose1 vs pose2 | blend | Observed |
|---|---|---|---|---|
| Weapon pickup on floor (grenade launcher) | 1 | equal (=0) | **0** | **BROKEN (always)** |
| Torch flame (`flame.mdl`,`longtrch.mdl`) — on `r_nolerp_list` | >1 | forced equal | **0** | **BROKEN (always)** |
| Held viewmodel, **idle** (incl. `v_saw.mdl` nolerp) | >1 | equal (paused) | **0** | **BROKEN** |
| Held viewmodel, **firing** | >1 | **differ** | **≠0** | **fine** |
| Walking monster (ogre/dog), animating | >1 | **differ** | **≠0** | **fine** |

`r_nolerp_list` default (`gl_rmain.c:98`) contains `flame.mdl, flame2.mdl, ..., longtrch.mdl, ...,
v_saw.mdl, v_xfist.mdl` — exactly the always-broken flames + fixed-frame viewmodels. `r_lerpmodels`
and `r_alias_lerpmode` both default to `1` (`gl_rmain.c:95,97`).

**The clean/broken split is precisely `blend!=0` (fine) vs `blend==0` (broken).** Equivalently:
whenever the two position attributes carry **identical data** (single effective pose), the model
breaks; when they carry two **different** poses that are interpolated, it renders correctly. This is
the "sometimes" for the viewmodel (breaks only while idle) and the "always" for pickups/torches
(never lerp).

Two candidate triggers remain, and they are almost perfectly correlated in Quake (whenever
`pose1==pose2`, the app both sets `blend=0` *and* the two attributes carry identical data). Read-only
cannot separate them, **but the existing `r_alias_lerpmode` cvar already can** (see §5):
- **T-A: `blend==0` per se** (the arithmetic/uniform value), or
- **T-B: the two active position attributes carry identical data** (a HW/fetch behavior when two
  enabled attribute records resolve to the same bytes).

---

## 4. Deliverable 2 — the Mesa byte-attribute translation path (cited), and why it is NOT the site

The full path from a `glVertexAttribPointer(GL_UNSIGNED_BYTE, ...)` to a V3D attribute record:

1. **`v3d_vertex_state_create`** (`external/mesa/src/gallium/drivers/v3d/v3dx_state.c:371-443`)
   pre-packs each `GL_SHADER_STATE_ATTRIBUTE_RECORD`:
   - `attr.vec_size = desc->nr_channels & 3` (`:395`) — 4-channel byte attr ⇒ `0` (== 4). ✓
   - `attr.signed_int_type = (type == SIGNED)` (`:396`) — `GL_BYTE` normal ⇒ signed; `GL_UNSIGNED_BYTE`
     pos ⇒ unsigned. ✓
   - `attr.normalized_int_type = desc->channel[0].normalized` (`:399`) — normals normalized, positions
     not. ✓
   - `r_size == 8` ⇒ `attr.type = ATTRIBUTE_BYTE` (`:426-428`). ✓ Both pose verts and normals are
     `ATTRIBUTE_BYTE`.
2. **Per-draw finalize** — `v3dx_draw.c:802-847` (`v3d_emit_gl_shader_state`) fills the live fields:
   - `attr.stride = elem->src_stride` (`:815`) = 8 (meshxyz_t). ✓
   - `attr.address = rsc->bo + buffer_offset + src_offset` (`:816-818`). ✓
   - `attr.number_of_values_read_by_coordinate_shader = cs->vattr_sizes[i]` (`:819-820`)
   - `attr.number_of_values_read_by_vertex_shader = vs->vattr_sizes[i]` (`:821-822`)
   - `attr.maximum_index = 0xffffff` (`:844`). ✓
3. **`vattr_sizes[i]`** is compile-time, per shader, in `ntq_setup_vs_inputs`
   (`external/mesa/src/broadcom/compiler/nir_to_vir.c:2356-2384`): the number of components each
   shader actually reads from attribute `i` (DCE-driven). For the alias program:
   - **Coordinate shader (CS, binner, position-only):** reads `Pose1Vert.xyz` (attr0) and
     `Pose2Vert.xyz` (attr2) ⇒ `vattr_sizes = [3,0,3,0,0]` (skips normals/texcoord).
   - **Vertex shader (VS, render):** reads all ⇒ `vattr_sizes = [3,3,3,3,2]`.
   The shader reads attribute `k` from VPM offset `Σ vattr_sizes[i<k]` (`nir_to_vir.c:2875-2889`);
   the fetcher DMAs each attribute's `number_of_values` consecutively into VPM in the same order.
   Both sides derive from the same `vattr_sizes`, so DMA layout and shader reads are consistent
   **per shader** (CS packs attr0@0, attr2@3; VS packs attr0@0,attr1@3,attr2@6,attr3@9,attr4@12).
4. **VPM/VCM sizing** — `v3d_compute_vpm_config` (`vir.c:3060-3088`) → `vcm_cache_size`
   (`vir.c:922-937`); shared input/output segments (`vir.c:910-920`). All compile-time.

**This entire path is stock upstream Mesa.** `git show 1584b1a06d9 -- v3dx_draw.c v3dx_state.c` shows
the Phoenix port commit added **only** (a) an EZ-reenable *comment* in `v3d_update_job_ez` and (b) an
R/B-swap *comment* in `v3d_set_framebuffer_state` — **zero** changes to attribute-record packing,
`number_of_values_read_*`, VPM, or VCM. `nir_to_vir.c` / `vir.c` are unmodified upstream. This is the
same code path Linux uses to run GLQuake on V3D 4.2 without this glitch, **including numposes==1
pickups that alias for real** (upstream QuakeSpasm has no de-alias). Therefore:

> **The vertex-element→attribute-record translation is not the bug site.** If a byte/aliased
> attribute were mistranslated here, Linux would glitch too, and the port did not touch this code.

The one structural feature unique to the alias program worth flagging (handled by stock Mesa, but
the only thing that is even *unusual* about this draw): the **coordinate shader reads a
non-contiguous subset of attributes `{0,2}`, skipping attr1** — the size-0 hole at attr1 in the CS's
`vattr_sizes`. Stock Mesa packs around it correctly (`nir_to_vir.c:2875-2887`), and Linux exercises
the identical pattern, so this is not asserted as the cause — but it is the natural thing for an HW
experiment to poke if §5's cvar experiments implicate the CS.

---

## 5. Deliverable — single most-likely root cause + ranked candidate fixes + HW discriminators

### Single most-likely root cause (best-supported; a proven line-bug is not reachable read-only)

> The corruption is a **V3D vertex-processing defect that manifests only for the single-effective-pose
> alias draw (`blend==0`, two active byte position attributes carrying identical data)** — **not**
> attribute aliasing (§2, refuted) and **not** the Mesa attribute translation (§4, stock + untouched
> + Linux-clean). It sits **below GLSL**, in the coordinate-shader / vertex-fetch execution for that
> specific draw shape, and it is **data-dependent** (only a *subset* of vertices spike ⇒ the wrong
> value is a function of the per-vertex bytes, not a global stride/format error).

The evidence pins the *condition* (`blend==0` / identical-data, §3) hard; it does not, read-only, pin
the exact HW mechanism (T-A vs T-B, §3). That split is what the first experiment below resolves.

### Candidate fixes, ranked

**Candidate 1 (RECOMMENDED — both the confirmation and a clean real fix): a dedicated single-pose
alias GLSL program that binds/reads only `Pose1`.**

- **Where:** `external/quakespasm/Quake/r_alias.c`. Add a second program in `GLAlias_CreateShaders`
  (`:120-210`) whose vertex shader declares **only** `TexCoords`, `Pose1Vert`, `Pose1Normal` (no
  `Pose2*`, no `Blend`, no `mix`) — i.e. `gl_Position = gl_ModelViewProjectionMatrix *
  vec4(Pose1Vert.xyz,1.0)`. In `GL_DrawAliasFrame_GLSL` (`:226-321`), when `blend==0` use that program
  and **do not enable / VertexAttribPointer the `pose2VertexAttrIndex` / `pose2NormalAttrIndex`
  arrays** (skip `:276,278,282,285`). When `blend!=0`, keep the existing two-pose program unchanged.
- **Why it is the right test:** it removes, for exactly the broken class, **both** candidate triggers
  at once — no second position attribute is enabled or read (kills T-B), and there is no `Blend`
  uniform / `mix` (kills T-A). It changes nothing for the already-correct animating (`blend!=0`) path.
- **Cheap to test:** it is a self-contained app-side change (no winsys/Mesa rebuild), on the exact
  path already under active iteration.
- **Prediction if this fixes it:** the grenade-launcher pickup renders as a **solid, properly
  textured and lit model spinning on the floor** (no black spikes/wedges); torch flames are solid
  animated sprites-on-mesh; the idle viewmodel is solid — while firing/animation stays as good as
  today. That outcome confirms the root cause is "two active byte position attributes / `blend==0`
  single-pose draw," and this *is* the fix (it also lets `gl_mesh.c`'s de-alias second block be
  reverted later as dead weight).

**Candidate 2 (advisor's #1 — tests the byte-fetch hypothesis directly): byte→float positions.**

- **Where:** `external/quakespasm/Quake/gl_mesh.c` — change `meshxyz_t.xyz` from `GLubyte[4]` to
  `float[3/4]` (fill at `:511-514`), and in `r_alias.c:281-282` change the `pose*VertexAttrIndex`
  pointers from `GL_UNSIGNED_BYTE`/`sizeof(meshxyz_t)` to `GL_FLOAT` with the new stride. Shader is
  unchanged (`Pose1Vert.xyz` reads the same either way). (Normals may stay `GL_BYTE`.)
- **Why:** isolates whether the *8-bit* fetch is the trouble vs any 4-byte-aligned attribute.
- **Prediction:** if byte→float **fixes** it, root cause = the `ATTRIBUTE_BYTE` vertex fetch on this
  silicon for this draw (and Candidate 1 works because float side-effect / dropping the 2nd attr).
  If it does **not** fix it (plausible — see §1/§3: at `blend==0` the fetch is identical regardless of
  which attr is unused, and firing uses the *same* byte fetch yet is clean), the trigger is the
  `blend==0`/two-active-attribute *condition*, not the byte width — pointing back to Candidate 1.
  Ranked below Candidate 1 because it is a larger data/VBO change and it does not test T-A at all.

**Candidate 3 (winsys, only if 1 & 2 both fail): the coordinate-shader submit shape for these draws.**
If a pure app-side change cannot move it, the residual is in the winsys/HW handling of the CS for
this draw (the non-contiguous `{0,2}` CS attribute set, or an EZ interaction — EZ was re-enabled by
the port, `671c4f08c95`). Lowest confidence; do not start here.

### Zero-cost HW discriminators that already exist (`r_alias_lerpmode`, `r_alias.c:245-258`) — run FIRST

These need **no code change**, just cvar sets in the running game, and they resolve T-A vs T-B and
prove the §3 correlation before any build:

1. **`r_alias_lerpmode 0`** (snap: forces `pose1=pose2` ⇒ `blend=0` for *all* models, `r_alias.c:245-246`).
   - **Predict:** *every* alias model — including walking monsters that are fine today — now spikes.
   - **Confirms:** the trigger is the single-pose/`blend==0` condition (§3), independent of whether a
     model naturally animates. (If monsters stay clean, §3 is wrong — but the class table makes that
     unlikely.)
2. **`r_alias_lerpmode 2`** (keeps the two **distinct** pose offsets bound + fetched, but forces
   `blend=0`, `r_alias.c:257-258`). Watch an **animating** monster (so pose1≠pose2, i.e. two attributes
   with **different** data, but `blend==0`):
   - **Spikes ⇒ T-A**: the trigger is `blend==0` itself (arithmetic/uniform), independent of the two
     attributes' data being equal. Fix = drop the `Blend`/`mix`/second-attribute at `blend==0`
     (Candidate 1).
   - **Stays clean ⇒ T-B**: the trigger is *identical data in two active attributes*. Fix = don't bind
     a second position attribute carrying identical data at `blend==0` (also Candidate 1). Either way
     Candidate 1 is indicated; this just tells you *why* it works.

**Suggested HW order:** run discriminators (1) then (2) on the DET/real quake build (no rebuild) to
lock §3 and pick T-A/T-B; then build **Candidate 1** and confirm the pickup/torch/idle-viewmodel
render solid across the demo. Keep the existing race fix-A in place (orthogonal; §0).

---

## Status
- [x] **Deliverable 1** — alias GLSL quoted (`r_alias.c:130-191`); **correct**; `Pose2` is provably
      unused at `Blend==0` (finite byte data ⇒ `mix(a,b,0)==a`).
- [x] **Deliverable 3 (headline)** — **aliasing REFUTED**: de-aliased numposes==1 pickup has distinct
      addresses + `blend==0` (position from `Pose1` only, no aliasing) and **still spikes**
      (`r_alias.c:245-282`, `gl_mesh.c:466-524`).
- [x] **Deliverable 2** — byte-attribute translation traced (`v3dx_state.c:371-443`, `v3dx_draw.c:802-847`,
      `nir_to_vir.c:2356-2389/2875-2889`, `vir.c:3060-3088/922-937`); **stock upstream, untouched by
      port `1584b1a06d9`** ⇒ **not the bug site** (Linux runs it glitch-free incl. real-aliasing pickups).
- [x] **Deliverable — root cause** — deterministic V3D vertex-processing defect for the
      **single-effective-pose (`blend==0`, two active byte position attributes, identical data)** alias
      draw; below GLSL; data-dependent (subset of vertices). Exact HW line not reachable read-only.
- [x] **Deliverable — fix + test** — Candidate 1 (single-pose GLSL program dropping `Pose2`) as the
      confirmation-and-fix; Candidate 2 (byte→float) as the byte-fetch discriminator; plus the
      zero-cost `r_alias_lerpmode 0`/`2` HW discriminators (T-A vs T-B) to run first. Predicted correct
      result: pickups/torches/idle viewmodel render as solid, textured, lit models with no black spikes.
</content>
</invoke>
