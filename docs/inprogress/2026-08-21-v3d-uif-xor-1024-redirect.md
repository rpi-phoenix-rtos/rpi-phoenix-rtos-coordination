# V3D UIF_XOR 1024² sampling bug — source ruled clean; it's a runtime magnitude effect (2026-08-21)

**Bug:** a UIF_XOR-tiled lightmap atlas samples correct at 512² but renders BLACK at 1024² on
V3D 4.2. Same class → quake3 lightmap-black + quake2 floor-speckle + vkQuake striping. Prior
`gl_uif_probe` hypothesis blamed the TMU texture-shader-state **descriptor** (a height bitfield
overflowing at 1024). **This turn REFUTES that** via two thorough source analyses.

## Mesa source is clean (subagent, primary-source)
- `v3d_setup_texture_shader_state` (v3dx_state.c:913-998): **no field overflows and none changes
  category 512→1024.** `image_width`/`image_height` are 14-bit (hold ≤16383); `array_stride` 26-bit.
  Only numeric deltas are height/width 512→1024 and stride 2048→4096 — all in-range.
- `v3d_setup_slices` (v3d_resource.c): UIF_XOR is chosen when `(height/8) % 32 == 0` (i.e. every
  256px multiple). **Both 512² AND 1024² are UIF_XOR with `ub_pad=0`, `padded_height==height`.**
  512↔1024 crosses NO UIF/XOR/LT boundary and adds no padding.
- **DECISIVE:** 512² is *itself* UIF_XOR and works — it already exercises the entire XOR path
  (`mb_y ^= 0x10`, `level_0_xor_enable=1`, UIFCFG=0x45's 8-bank/4KB/bit-4 XOR addressing). If
  UIFCFG / the XOR bit / bank config / the `uif_pixel_off` formula were wrong, **512² would also
  corrupt.** It doesn't. ⇒ XOR mechanism, UIFCFG=0x45, and the descriptor XOR fields are all
  EXONERATED in one shot.
- GL (`v3dx_state.c`) and Vulkan (`v3dvx_image.c`) derive the same fields identically — consistent
  with the bug hitting both GL (quake3/2) and Vulkan (vkQuake).

## Phoenix winsys is also size-correct (this session, reading v3d_phoenix_winsys.c)
- Texture BOs use the **uncached** path (`MAP_UNCACHED`; cacheable+dc-ivac path is only for
  CPU-readback RTs) → no CPU-cache-flush-coverage bug for textures.
- PT-fill is **per-page via va2pa** over the full `pages` (`:612-614`) → a 4MB BO's every page is
  mapped correctly even if physically non-contiguous. Full coverage.
- `mmap` failure is checked (`:594-597`, logs + -ENOMEM) → a failed 4MB alloc wouldn't silently
  corrupt. BO is zeroed over its full span (`:606`).
- L2T flush range is the WHOLE cache (`L2TFLSTA=0, L2TFLEND=~0`, `:808`) → not a partial span.
- GPU VA is bump-allocated inside the flat PT window (32-bit by design); `texture_base_pointer =
  rsc->bo->offset` (= gpuva) is inherently 32-bit-safe.

## ⇒ Conclusion: runtime magnitude effect, not a static-source value
1MB (512²) works, 4MB (1024²) fails, and both source layers handle 4MB correctly by inspection.
So the divergence is at RUNTIME on the 4MB BO. Top suspects (need HW data to pick):
- a truncated / wrong packed `texture_base_pointer` at the actual high VA the 1024 atlas lands at;
- `MAP_CONTIGUOUS` behavior / `va2pa` for a 4MB allocation (does mmap actually satisfy 4MB, and are
  the per-page PAs what the PT records);
- a stale-cache / coherency window specific to the larger working set ("black" = TMU reading
  unmapped/stale pages).

## NEXT (executable) — runtime descriptor+slice dump (bypasses the flaky render path)
The dump fires at TEXTURE SETUP (which works — only the `gl_uif_probe` *render* is flaky), so any
GL app that creates the 512 + 1024 atlases yields the comparison. Add to
`external/mesa/src/gallium/drivers/v3d/v3dx_state.c` at the end of `v3d_setup_texture_shader_state`
(after line 997, before `}`), guarded to large textures (add `#include <stdio.h>` if needed):

```c
if (prsc->width0 >= 512 || prsc->height0 >= 512) {
    fprintf(stderr,
        "V3DTEX w=%u h=%u | bo.offset=0x%08x base_offset=0x%08x | "
        "slice0 tiling=%d stride=%u padded_h=%u offset=%u ub_pad=%u | xor=%d uif=%d\n",
        prsc->width0, prsc->height0,
        (unsigned)rsc->bo->offset, (unsigned)base_offset,
        rsc->slices[0].tiling, (unsigned)rsc->slices[0].stride,
        (unsigned)rsc->slices[0].padded_height, (unsigned)rsc->slices[0].offset,
        (unsigned)rsc->slices[0].ub_pad,
        tex->level_0_xor_enable, tex->level_0_is_strictly_uif);
}
```
Then: `python3 tools/v3d-driver-port/build-v3d-phoenix.py` (rebuild libv3d — HOSTBUILD present, so
incremental) → build the gl_uif_probe (`build-gl-uif.py`) → deploy to netboot /bin → run at psh →
capture `V3DTEX` lines for the 512 and 1024 atlases. **Expected (correct):** 1024 → `stride=4096
padded_h=1024 tiling=UIF_XOR(=? enum) xor=1`, `bo.offset` a sane 4KB-aligned VA. Any deviation
(truncated base_offset, wrong stride/padded_h, xor=0, a non-aligned/huge VA) is the bug. Also add
a matching dump in the winsys BO-alloc (gpuva/size/pa + whether the 4MB `mmap(MAP_CONTIGUOUS)`
returned contiguous PAs) to catch an allocation-side fault.

If the descriptor + slice + BO all read correct at 1024, the next discriminator (subagent's) is two
1MB axis-split probes (1024w×256h vs 256w×1024h — verify both compute to UIF_XOR ub_pad=0) to split
width/stride vs height, and single-`glTexImage2D` vs many-`glTexSubImage2D` to separate the sample
path from upload accumulation.

**Status:** the 2-year-old "descriptor mis-encode" hypothesis is REFUTED; the bug is a runtime
4MB-BO magnitude effect; the precise HW instrumentation to pin it is specified above. GPU HW test
is semi-attended (netboot + UART capture; no HDMI needed for the V3DTEX dump).

## 🎯 ROOT CAUSE FOUND + FIXED (2026-08-21, HW descriptor-confirmed)

Ran the V3DTEX dump on HW. Ground truth (SAMPLE data ignored — the probe's render path is a known
broken harness, all-black even at 512; the descriptor/slice dumps are what's trustworthy):

```
pre-fix 512 : slice0 tiling=5(UIF_XOR) stride=2048 xor=1 uif=1   (correct — renders fine)
pre-fix 1024: slice0 tiling=0(RASTER)  stride=4096 xor=0 uif=0   <-- WRONG: not tiled!
V3DBO 1024  : size=4100KiB contig=1                              (4MB BO IS contiguous — alloc fine)
```

**The 1024 atlas was being laid out as RASTER (linear), not UIF_XOR** — so the TMU sampled linear
data as if UIF and read garbage/black. Root cause = the Phoenix `should_tile` optimization in
`v3d_resource.c` (~:905): it forces a full-screen color RT to RASTER for fast `glReadPixels`, gated
on `(RENDER_TARGET && width>=1024 && height>=768)`. But Mesa marks every renderable (RGBA8) texture
`PIPE_BIND_RENDER_TARGET`, so a **large SAMPLED texture** (the 1024² merged lightmap atlas) also
matched → forced RASTER. The 512² atlas escaped only because width<1024 (→ stayed UIF_XOR → worked).

**Fix (external/mesa `4363822955b`):** add `!(tmpl->bind & PIPE_BIND_SAMPLER_VIEW)` to the gate —
a texture the TMU will sample must stay tiled; only a pure non-sampled RT may go RASTER.

**Post-fix HW confirmation (fresh gl_uif_probe on the rebuilt libv3d):**
```
post-fix 1024: slice0 tiling=5(UIF_XOR) stride=4096 xor=1 uif=1   <-- now identical to the working 512
```
The 1024 atlas now takes the exact tiling path as the known-good 512 atlas ⇒ the read-side is fixed
at the descriptor level. This is THE class fix (large sampled textures >=1024x768): quake3
lightmap-black + quake2 floor-speckle + vkQuake striping.

## ✅ VISUAL CONFIRM + WORKAROUND REMOVED (2026-08-21) — bug FULLY RESOLVED

Rebuilt quake3e (169/169 TUs) against the fixed `libv3d-phoenix.a`, deployed it, and ran q3dm7
with `r_mergeLightmaps 1` (merged 1024² atlas — the previously-black path). HDMI grabs show a
colorful (not-black) scene but with heavy uniform horizontal-line tearing. **A/B discriminator:**
ran q3dm7 with merge=0 (the known-lit, owner-HW-verified workaround path) — its HDMI grab shows the
**identical** uniform striping and the **same** colors/brightness. ⇒ the striping is purely the
HDMI capture-card artifact (present even in the known-good reference), and the merge-ON (fixed)
render is **visually equivalent to the known-lit merge-OFF render** ⇒ the lightmaps are lit ⇒ the
fix works. (A black-lightmap regression would render dark; both frames are mid-tone colorful and
indistinguishable.)

**Two independent confirmations:** (1) descriptor-level — post-fix the 1024 atlas is UIF_XOR,
identical to the working 512; (2) visual A/B — merge-ON ≡ known-lit merge-OFF in the HDMI capture.

**Workaround REMOVED:** quake3-launcher.c no longer forces `r_mergeLightmaps 0`; quake3e's default
(merged atlas) is used, rebuilt launcher deployed.

**SCOPE CORRECTION (which renderers this fixes):** the fix is in the **gallium GL** path
(`v3d_resource.c` `should_tile`), so it covers the GL-path apps: **quake3 (confirmed)** and **quake2
(likely — yQuake2 ref_gl1 is GL; its "floor-speckle" residual should be re-tested against the fixed
lib, though it may be a separate slow-TFU-load artifact)**. It does **NOT** touch **vkQuake**, which
uses the separate **V3DV (Vulkan)** path — `v3dv_image.c` has stock upstream tiling with NO Phoenix
RASTER-forcing mod, so vkQuake's striping is a **separate, still-open V3DV read-side issue** (the
MASTER plan already retired the "one unified bug" hypothesis on this basis). Earlier commit/doc
wording that this fix "resolves vkQuake striping" was inaccurate and is corrected here.

**Status: GL-path RESOLVED.** V3D large-UIF_XOR *GL* sampling bug root-caused (should_tile forcing
large sampled textures to RASTER), fixed (exclude SAMPLER_VIEW, external/mesa 4363822955b), confirmed
at the descriptor level and visually (A/B vs the known-lit reference). quake3 lightmap-black closed;
quake2-GL likely closed (re-test); vkQuake-V3DV striping remains a separate open dig.
