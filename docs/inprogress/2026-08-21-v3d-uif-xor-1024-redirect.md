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
