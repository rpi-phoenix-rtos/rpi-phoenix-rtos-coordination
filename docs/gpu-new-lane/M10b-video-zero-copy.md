# M10 zero copy: hardware-decoded HEVC to the GPU without the CPU

**Status (2026-10-08):**
- **Checkpoint 1 PASSED on the Pi** (build 58, §7.2). The coordinator decided on path B even though the SAND blit takes 3.9 ms, over the 2 ms bar: it is GPU time, and it saves ~11 ms of CPU per picture.
- **Step 2, the decoder side, is merged** (§7.3, ports `e4d9a3c`). It is host-tested and cross-compiled, and not yet run on the Pi.
- **P31 is closed** (§7.3): after leaving the block, the decoder drops pictures until the next IRAP.
- **Step 3, the WebKit side, ran on the Pi in build 60 (§7.5) and drew nothing zero-copy** (`zc_painted=0`). WPE composites with Skia, and 0033 had imported only on the TextureMapper path. It is fixed on ports branch `webkit-zero-copy` (§7.5), host-built but not run yet. Zero copy is opt-in (`WPE_PHOENIX_MEDIA_ZERO_COPY=1`).
- **The block is taken again after a fallback** (bounded retries, §7.5): one refused picture no longer costs the rest of the video.
- **Builds 62/63** (§7.6): zero copy works on the Pi (PeerTube 1080p60 painted 33 → 49/s, web process CPU 34 → 16 %). The MSE arm's `zc_painted=0` and the block timeouts trace to a lost-interrupt race in `rpivid_hw.c` (pre-existing), fixed on ports branch `webkit-zero-copy`.
- **Round 4** (§7.7): the block reaches all of a Pi 4's RAM, so the high buffer addresses are not the cause (and the `0xf0400000` buffer is WebKit's swap chain, not a picture). A block that stops responding is now recorded for the boot (`/tmp/.rpivid.dead`), its clock goes off, and every later player decodes on the CPU at once, without touching it.
- **Not started:** the players.

Related documents:
- [M10-hevc-hwaccel.md](M10-hevc-hwaccel.md) covers the `hevc_rpivid` decoder itself. Its §"SAND → planar cost", option 3, is this document.
- [M10-video-player.md](M10-video-player.md) §4 options 2 and 3 first sketched the zero-copy GL path and the KMS plane path.
- [B8-video.md](../browser/B8-video.md) and [MSE-DESIGN.md](../browser/MSE-DESIGN.md) describe the browser player.
- The weekly log ([WEEK-2026-W41.md](../inprogress/WEEK-2026-W41.md):153) already sequences this work "after MSE lands".

Notation used below:
- `B` is the Mesa tree the `mesa_drm` port builds: `.buildroot/_build/aarch64a72-generic-rpi4b/port-sources/mesa_drm-26.2.0/mesa-26.2.0`. That is upstream 26.2.0 plus our patches, and the SAND code has none of them.
- `RV` is `sources/phoenix-rtos-ports/video_player/files/rpivid/src`.
- `V3DA` is `sources/phoenix-rtos-devices/gpu/rpi4-v3d-async`.
- `LDP` is `sources/phoenix-rtos-ports/libdrm_phoenix/glue/phoenix`.
- `WK` is the WebKit 2.54 source the port unpacks: `.buildroot/_build/aarch64a72-generic-rpi4b/webkit_wpe-build/src/webkit`.
- `0030` is `sources/phoenix-rtos-ports/webkit_wpe/patches/webkit-video/0030-wpe-phoenix-ffmpeg-media-player.patch`, with line numbers inside the patch file.

---

## 0. Recommendation (summary)

**Path B: the decoder writes into render-server BOs, and the compositor samples the SAND buffer directly.**

The decoder allocates its picture pool as **V3D BOs on the render node** (`DRM_IOCTL_V3D_CREATE_BO`). Each BO is one physically contiguous block whose physical address the client already receives. The block decodes straight into a BO, laid out as single-buffer NV12 SAND128 (luma and chroma in the same 128-byte column, the layout of Linux's `NV12_COL128`).

`hevc_rpivid` then outputs `AV_PIX_FMT_DRM_PRIME` frames that hold a reference to the BO. A consumer has to opt in to get them. The WebKit layer buffer imports each pool BO once as an `EGLImage` (`DRM_FORMAT_NV12` + `DRM_FORMAT_MOD_BROADCOM_SAND128_COL_HEIGHT(h)`) and draws it with `drawTextureExternalOES`. Mesa's upstream `v3d_sand8_blit` (a fragment-shader blit) de-tiles it on the GPU, and `nir_lower_tex` converts YUV to RGB.

**What it removes:**
- the 6.3 ms of CPU SAND→planar per 1080p picture;
- the ~5 ms of `glTexImage2D` / `v3d_store_utile` per picture.

That is about 11 ms of CPU per picture: ~68 % of one core at 60 fps on the per-picture timers, and roughly 95 % of a core in the `prof` attribution.

**What it adds:**
- GPU work for the SAND blit, estimated at 1–2 ms per picture (checkpoint 1 measures it);
- a few IPCs per picture (`WAIT_BO`).

**No kernel, render-server or libdrm-phoenix change is needed.** The new code is in the rpivid hwaccel (FFmpeg port) and in a new WebKit patch `0032`.

**Path A** (CPU de-tile into a LINEAR BO) is not the cheap intermediate it looks like, for two reasons:
- Shared BOs must be uncached, so the CPU would write 3 MB per picture through Normal-NC memory.
- Mesa still makes a tiled shadow copy of every LINEAR import on every draw.

It shares all of B's plumbing and saves only the upload. It stays as the fallback if the SAND blit proves wrong on our geometry.

**Path C** (an HVS overlay plane scanning SAND out) is the zero-GPU option for full-screen `video-play` only. It needs `rpi4-kms` to accept NV12/SAND framebuffers. Do it after B, reusing B's BO pool.

**Effort:** about 6 agent-days to a browser gate pass for B, plus about 2 for the players (§6). This excludes WebKit rebuild wall-clock: one WebKit relink per WebKit checkpoint, which dominates elapsed time.

---

## 1. Where the time goes today (build 56–57, measured)

| Stage | Where | Cost per 1080p picture | At 60 fps |
|---|---|---|---|
| Block decode | rpivid hardware, interrupt completion | 2.4–4.2 ms (wall clock, not CPU) | — |
| SAND → planar | CPU, decoder thread: `rpivid_sand8_to_planar` (`RV/rpivid_sand.c:102-139`), called from `rpivid_end_frame` (`RV/rpivid_hevc.c:1078-1088`) | **6.3 ms** after the row-order fix (was 8.9 ms) | 38 % of a core by the timer; `prof` attributed ~70 % (W41:153, measured before the fix) |
| Plane upload | CPU, compositing thread: `glTexImage2D` ×3 in `CoordinatedPlatformLayerBufferFFmpeg::paintToTextureMapper` (`0030`:298-330), tiled by Mesa (`v3d_store_utile`) | **~5 ms** | ~26 % (`prof`) |
| Result | — | — | presents 60, paints ~35 frames/s |

**Why the copies exist:**
- The hwaccel is a "system-memory" hwaccel. Its pixel format is `AV_PIX_FMT_YUV420P` (`RV/rpivid_hevc.c:1126`). It de-tiles into the frame `hevcdec.c` allocated.
- The SAND buffers are a private pool: `pool_get` allocates them as two separate cached `MAP_CONTIGUOUS` blocks (`RV/rpivid_hevc.c:183-189`, `RV/rpivid_hw.c:307-357`).
- Luma and chroma have different column strides: `rpivid_geom`, `RV/rpivid_cmd.c:658-678`, luma stride = `H16 × 128` and chroma = half that.
- WebKit's FFmpeg layer buffer only knows system-memory planes (`0030`:217-227).

**Process layout (it matters for sharing):**
- WPE runs with `ENABLE_GPU_PROCESS=OFF`, `USE_GBM=OFF` (`.buildroot/.../webkit_wpe-build/webkit-build.options`).
- The decoder threads (`MediaPlayerPrivateFFmpeg::videoThread`, `0030`:1791-1904) and the threaded compositor both live in the **WebProcess**.
- The only other process on the path is the render server `rpi4-v3d-async`.
- The composited page leaves the WebProcess as a `/v3dbuf` dma-buf to labwc (patch `webkit/0016`). That part is unchanged here.

---

## 2. Mesa: can v3d import and sample these buffers?

Our Mesa is upstream **26.2.0** (`mesa_drm` port, tarball). None of our 18 patches touch `v3d_resource_from_handle`, the modifier tables, `v3d_bo_open_dmabuf` or the SAND blits. The SAND code in `B` is the upstream code, and it is compiled: `v3d_blit.c` is built once into the version-independent `libv3d.a`. Every `B` line number below was checked in the built tree.

### 2.1 EGL entry points

- `EGL_EXT_image_dma_buf_import` and `…_modifiers` are advertised when `pscreen->caps.dmabuf` is set (`B/src/egl/drivers/dri2/egl_dri2.c:628-630`, `:719-722`).
- On Phoenix `caps.dmabuf` is set only thanks to our patch `mesa_drm/patches/0008` (`DRM_CAP_PRIME` queried on Phoenix).
- The Pi log of the extension list is in [M3-libdrm-phoenix.md](M3-libdrm-phoenix.md):1143 and :1349.
- WebKit's surfaceless display already uses the sibling `EGL_MESA_image_dma_buf_export` (`webkit/0016`), so the import side comes from the same capability.

### 2.2 (a) LINEAR R8 / GR88 / NV12 / YUV420: yes, but always through a GPU shadow copy

- **Import.** `DRM_FORMAT_MOD_LINEAR` is accepted (`B/.../v3d/v3d_resource.c:972-973`). A plane offset is allowed for non-tiled layouts, and the stride is taken as given.
- **The TMU cannot sample raster textures.** `v3d_create_sampler_view` creates a **tiled shadow** for any raster texture that is not 1D or a buffer (`B/.../v3d/v3dx_state.c:1152-1188`).
- **The shadow is refreshed at draw time** by `v3d_update_shadow_texture` (`v3dx_draw.c:148-150` → `v3d_resource.c:1077-1093`). It skips the refresh only when `shadow->writes == orig->writes && orig->bo->private` (`:1087`).
- An imported BO is never private (`v3d_bufmgr.c:450`), so **every draw re-blits every plane**.
- For linear R8/RG8 the blit is done by the TFU (`v3dx_format_table.c:344` lists R8 and RG8 as TFU formats).
- **Multi-plane formats.** v3d has no native NV12/YUV420 sampling. The dri frontend lowers NV12 to R8 + GR88 and YUV420 to 3 × R8, one `resource_from_handle` per plane (`B/src/gallium/frontends/dri/dri2.c:821-883`).
- **External-only.** NV12 and YUV420 are external-only for every modifier (`dri2.c:1345-1372`, `v3d_screen.c:697`). They must be bound to `GL_TEXTURE_EXTERNAL_OES`, and the state tracker adds the YUV→RGB in `nir_lower_tex` (`lower_y_uv_external`, `B/src/mesa/state_tracker/st_program.c:1196`).
- Single-plane R8 and GR88 with LINEAR can be bound to `GL_TEXTURE_2D` (not external-only, `v3d_screen.c:669-680`). That is what WebKit's own YUV shaders (`drawTexturePlanarYUV`) would need.

### 2.3 (b) `DRM_FORMAT_NV12` + `DRM_FORMAT_MOD_BROADCOM_SAND128`: yes, upstream, on V3D 4.2

**Modifier tables.** `v3d_available_modifiers = {UIF, LINEAR, SAND128}` (`B/.../v3d/v3d_screen.c:638-642`).
- `query_dmabuf_modifiers` exposes SAND128 for NV12, R8, RG88, R16 and RG1616, always external-only. P030 gets SAND128 only (`:645-699`).
- `is_dmabuf_modifier_supported` accepts the parameterised `SAND128_COL_HEIGHT(h)` through `fourcc_mod_broadcom_mod()` for those formats (`:702-743`).

**Import** (`B/.../v3d/v3d_resource.c:958-1032`):
```c
case DRM_FORMAT_MOD_BROADCOM_SAND128:            /* :981, the bare modifier */
        rsc->tiled = false;
        rsc->sand_col128_stride = whandle->stride;   /* the EGL PITCH is taken as the column height */
        break;
default:
        switch (fourcc_mod_broadcom_mod(whandle->modifier)) {
        case DRM_FORMAT_MOD_BROADCOM_SAND128:    /* :987, SAND128_COL_HEIGHT(h) */
                rsc->tiled = false;
                rsc->sand_col128_stride = fourcc_mod_broadcom_param(whandle->modifier);
```
- **The column height is required.** It is either the modifier's parameter or, with the bare modifier, the plane pitch.
- **Use the parameterised form, with pitch = width × cpp.** The uapi says so (`drm_fourcc.h`, Broadcom SAND comment: "set the stride to width*cpp"; "the column height … is the same for all of the planes, assuming that each column contains both Y and UV"). The bare form reuses `whandle->stride` both as the column height and as the slice's byte stride (`v3d_setup_slices(screen, rsc, whandle->stride, true)`, `:1017`). That is only consistent by accident.
- The parameter is in **lines (rows of 128 bytes)**. The firmware plane API uses the same convention (`external/linux/drivers/gpu/drm/vc4/vc4_firmware_kms.c:648-651`: "the column pitch is passed across in lines").
- **One parameter covers both planes.** So the shader expects luma and chroma to share one column stride: the Linux `NV12_COL128` single-buffer layout (§4.1).
- Plane offsets are allowed because the resource is not tiled (`:1020-1037`).

**The de-tile is `v3d_sand8_blit`** (`B/.../v3d/v3d_blit.c:829-920`), called from `v3d_blit` (`:1237-1259`, first in the chain after `sand30`).
- It is a **fragment-shader blit** through `util_blitter_custom_shader`, not compute and not TFU.
- The SAND BO is bound as fragment **constant buffer 1** and read with `nir_load_ubo`, 32 bits per fragment. The column height is a uniform (`:741-748`, `:802-810`; addressing `stripe = (x >> 6 or 5) * stride << 7`, `:777-801`).
- It writes a UIF-tiled R8 (luma) or RG8 (chroma) destination, viewed as RGBA8.
- It accepts only `R8_UNORM` / `R8G8_UNORM` sources (`:839-841`), which are exactly the planes the NV12 lowering produces.
- `v3d_sand30_blit` (`:1141-1231`) does the same for 10-bit P030 → P010 (R16 / RG1616).

**No version guard.** `#define V3D_VERSION 42` at `v3d_blit.c:35` only selects the format table. The shaders are plain NIR. The path was written for the Pi 4 HEVC decoder's `NV12_COL128` output on Linux (not verified here which Raspberry Pi OS players use it).

**Full call chain** (each line verified in `B`):

| Step | Code |
|---|---|
| `eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT)` | `dri2_create_image_dma_buf` `egl_dri2.c:2434` → `dri2_from_dma_bufs` `dri2.c:1407` |
| planes from the modifier | `dri2_get_modifier_num_planes` `:916-943` → 2 |
| lowering | `dri_create_image_from_winsys` `:686`, `use_lowered` `:821-831` |
| per-plane import | `resource_from_handle` ×2 `:881` → `v3d_resource_from_handle` → `v3d_bo_open_dmabuf` `v3d_resource.c:1004` |
| bind | `glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES)` → `st_bind_egl_image` `st_cb_eglimage.c:446` (NV12: 2 texture units) |
| sampler views | `st_atom_texture.c:59`, `:197-207` → `v3d_create_sampler_view` → shadow (`v3dx_state.c:1155-1188`) |
| draw | `v3d_update_shadow_texture` (`v3dx_draw.c:150`) → `pctx->blit` → `v3d_sand8_blit` |

**Costs and consequences:**
- **Per-draw:** the blit runs on every draw that samples the image, not once at import (the `private` rule above). A paused video that is recomposited, for example during a scroll, is re-blitted each time. This is GPU work only. §4.2 covers the option to blit once per picture.
- **Per sampler view:** each new texture or sampler view creates a fresh tiled shadow (R8 1920×1088 + RG8 960×544 ≈ 3.1 MB). Creating an `EGLImage` and texture **per picture** would therefore allocate and free 3 MB through the render server every frame. **Cache one texture per pool BO** (§4.2).
- **renderonly:** `v3d_resource_from_handle` also does a renderonly KMS import when `screen->ro` is set (`v3d_resource.c:1039-1048`). WebKit's display is surfaceless on `renderD128` (plain v3d screen, `ro == NULL`), so this does not apply. kmsro users (SDL KMSDRM, labwc) do get one KMS import per imported BO, a one-time cost with caching. Patch 0012 only changes allocation and is opt-in.

**A faster option for later.** The V3D **TFU** has a SAND128 input format (`B/src/broadcom/common/v3d_tfu.h:47-49`: `V3D33_TFU_ICFG_FORMAT_SAND_128`), and the Linux TFU job carries YUV→RGB coefficients. A TFU NV12-SAND → RGBA8/UIF conversion would replace the shader blit with fixed-function work. Mesa does not use it. It is an optimisation only if checkpoint 1 shows the shader blit is too slow.

### 2.4 (c) 10-bit

- P030 + SAND128 imports and de-tiles through `v3d_sand30_blit` to P010.
- Mesa lowers external P010 like NV12 (R16 + RG1616).
- Today 10-bit costs a CPU SAND→planar16 (`rpivid_sand10_to_planar16`) plus a `libswscale` 10→8-bit conversion in WebKit (`0030`:1752-1782).
- Zero copy would remove both. It is a follow-up to the 8-bit path (§7, step 6).

---

## 3. Phoenix buffer sharing: what a dma-buf is, and how the decoder gets one

### 3.1 The mechanism today

**Kernel export (E1).** `memExport(oid, va, size)` publishes a range of the caller's anonymous `MAP_CONTIGUOUS` mapping under an oid on a port the caller owns.
- It is implemented by `vm_objectExport`, which creates an export window that borrows the parent's pages and holds a reference on it (`sources/phoenix-rtos-kernel/vm/object.c:1075-1158`).
- `mmap()` of a descriptor for that oid maps the same pages. Mapping with a cache type different from the export's is refused (`object.c:1255`).
- Pages are freed only after unexport **and** the last unmap (`:516-571`).
- See [E1-vm-object-export.md](E1-vm-object-export.md).

**A "dma-buf fd"** is an ordinary descriptor from `open("/v3dbuf/<id>")` (the render server's BO export) or `open("/kmsbuf/<id>")` (an `rpi4-kms` dumb buffer). It passes between processes over `SCM_RIGHTS`.
- libdrm-phoenix recognises one **only by that path prefix** (`LDP/xf86drm_phoenix.c:1160-1170`).
- It assumes uncached memory for any descriptor this process did not export itself (`:1178`).

**BO create.** Every BO is one `MAP_CONTIGUOUS` block (`V3DA/v3da_bo.c:92-110` `block_map`; `v3da_proto.h:345-346`). Its pages go into the V3D MMU one PTE per page (`v3da_bo.c:323-327`):
```c
uintptr_t ppa = (i < scan_pages) ? ((uintptr_t)buf_pa + (uintptr_t)i * _PAGE_SIZE) :
        (uintptr_t)va2pa((char *)cpu + (size_t)i * _PAGE_SIZE);
srv.hw.pt[(gpuva >> V3D_PAGE_SHIFT) + i] = (uint32_t)(ppa >> V3D_PAGE_SHIFT) | PTE_W | PTE_V;
```
- `BO_MMAP` of a client's own BO answers `V3DA_MEM_PHYS` with `addr = b->pa`: **the client learns the physical address** (`v3da_bo.c:693-698`).
- libdrm-phoenix creates **uncached** BOs only (`LDP/drm_phoenix_v3d.c:738-760`).
- `V3DA_BO_LOWMEM` (proto 5) places a BO below 1 GiB, for firmware scan-out (`v3da_proto.h:262-278`).

**PRIME export.** `V3DA_OP_BO_EXPORT` runs `memExport` on the BO's block under `{/v3dbuf port, handle}` (`v3da_proto.h:342-360`). libdrm returns `open("/v3dbuf/<h>")` (`LDP/drm_phoenix_v3d.c:1452-1524`).
- Cacheable, scanout and imported BOs are refused (`v3da_bo.c:1080-1086`). The comment there explains why: a dma-buf descriptor cannot carry a memory type.

**PRIME import.** `ioc_prime_import` (`LDP/drm_phoenix_v3d.c:1379-1439`) handles two cases:
- A `/v3dbuf` name gives the importer **the same BO**: same handle, same GPU VA, same last-use record, plus a reference (`v3da_proto.h:315-319`).
- A handle the connection already holds is returned with no IPC (`:1400-1409`).
- A `/kmsbuf` name is mapped by the server, which then resolves each page with `va2pa` (`v3da_bo.c:832-886`).
- **There is no third namespace, no userptr and no import by physical address.**
- `shmsrv` (`/shm/<id>`, `tools/gpu-lane/weston-drm/shmsrv/shm_proto.h:20-40`) is cached `MAP_CONTIGUOUS` memory exported with `memExport` for `wl_shm`. It is not a dma-buf: libdrm does not recognise `/shm/`, and it is cached. It plays no part here.

**Lifetime.** A BO's references are its creator handle, sharer imports and open `/v3dbuf` descriptors (`v3da_bo.c:455-466`). At zero references the export is withdrawn and the BO is quarantined until every queue has passed its last use (`:396-422`, `:632-677`). An importer therefore keeps the pages alive until the GPU is done with them.

**GPU caches.** Every job does `dsb sy`, slice-cache invalidate, TLB flush and L2T flush/invalidate before the kick (`V3DA/v3da_jobs.c:398-412`), and the slices are invalidated again at render start (`:438`). So a BO that DMA rewrote between jobs is never read stale through V3D caches.

**Synchronisation (G6).** The per-BO last-use record works as a reservation object. Once a BO is exported, `WAIT_BO` on it asks the server and sees **every** client's jobs (`LDP/drm_phoenix_v3d.c:821-838`, [G6-cross-process-sync.md](G6-cross-process-sync.md)). rpivid writes never go through this.

### 3.2 The decoder's options

| | How | New code | Verdict |
|---|---|---|---|
| **(i) Allocate from the render server** | The decoder opens `renderD128` itself, `CREATE_BO` (size of one picture), `MMAP_BO`, and gets the PA from the `MEM_PHYS` memref (or `va2pa` of the mapping). rpivid writes there (it takes `pa >> 6`, `RV/rpivid_regs.h:107`: no 4 GiB limit). `drmPrimeHandleToFD` → `/v3dbuf/<h>` for Mesa. | rpivid allocator only | **Recommended.** Contiguous by construction; zero kernel, server or libdrm work; G6 `WAIT_BO` covers reuse; `V3DA_BO_LOWMEM` makes the same BO scan-out-able for path C. |
| (ii) Export the decoder's own blocks | The WebProcess registers a port and a `/vdecbuf` namespace, runs `memExport` per block and a namespace thread (shmsrv-shaped, ~200 lines). libdrm gets a third prefix and namespace (~40 lines); `v3da_bo_import` and `import_map` get a third directory (~30 lines). | kernel no; server, libdrm, WebProcess yes | Only if the pictures must stay **cached**, which also needs CPU cache maintenance on import that nothing does today. Reuse sync is not covered by G6. |
| (iii) "BO from a physical range" op | `mmap(MAP_PHYSMEM, pa)` in the server plus the PTE loop | ~30 lines | **No.** Nothing ties the GPU mapping's lifetime to the pages: the decoder's `munmap` frees pages that the V3D MMU still points at (E1 §7, the `MAP_PHYSMEM` hole). |

**Consequences of (i):**
- **The SAND buffers become uncached.** On the zero-copy path the CPU never reads them. The block writes them by DMA, V3D reads them by DMA, and their CPU mapping is used only for a fallback or readback de-tile. That de-tile reads uncached memory and costs ~8–10 ms per 1080p picture ([M10-hevc-hwaccel.md](M10-hevc-hwaccel.md):65, :193-194). That is acceptable for the rare cases in §4.1.
- **Contiguity guard.** At pool allocation, check once that `va2pa` of every page is `pa0 + i × 4096`. On failure the decoder stays on today's private-pool path, so a later server-allocator change cannot silently corrupt DMA.
- **Coherency.** The block writes into memory the CPU has never cached (uncached BO), and the block's completion interrupt precedes any GPU submit that samples the picture: `rpivid_hw_decode` is synchronous (`RV/rpivid_hevc.c:1066`). V3D invalidates its own caches per job (above). **No cache maintenance is needed on the zero-copy path.** Today's `dc civac` (`RV/rpivid_hw.c:330-363`) is kept only for the private-pool path.

---

## 4. The three paths, end to end

### 4.1 Common decoder change: the pool as V3D BOs, the single-buffer SAND layout, and a DRM_PRIME frame

**Layout.** Use one BO per picture, laid out as Linux `NV12_COL128`:
- column height `C = H16 + H16/2` lines, where `H16 = (height + 15) & ~15`;
- luma at offset 0, chroma at `H16 × 128`;
- **both** strides = `C × 128`.

For 1080p: `C = 1632` and 15 columns, so 3.13 MB per picture, the same as today's two blocks.

The block takes independent `OUTYBASE`/`OUTCBASE` and `OUTYSTRIDE`/`OUTCSTRIDE`, and per-reference strides as well (`RV/rpivid_hw.c:627-635`). So the change is confined to `rpivid_geom` (`RV/rpivid_cmd.c:658-678`, one stride for both planes plus a chroma offset) and to how `pool_get` fills `y`/`c`. This layout is what both Mesa's one-parameter SAND import (§2.3) and the HVS's one-pitch plane (§4.4) require.

**Frames.**
- Add a decoder option `rpivid_out=planar|drm_prime` (default `planar`, so ffplay, gtk-video and `hevc-rpivid-check` are untouched).
- With `drm_prime`, the hwaccel gets an `FFHWAccel.alloc_frame`. FFmpeg 6.1 calls it instead of `get_buffer2` for hwaccel frames (`ffmpeg-6.1/libavcodec/decode.c:1664-1668`, `hwaccel_internal.h:43`). No `hw_frames_ctx` and no `--enable-libdrm` are needed (`CONFIG_LIBDRM 0` today).
- `alloc_frame` takes a BO from the pool (moving today's `pool_get` from `ff_rpivid_hevc_picture_ok`, `RV/rpivid_hevc.c:786`). It sets `frame->format = AV_PIX_FMT_DRM_PRIME`, `frame->data[0]` = an `AVDRMFrameDescriptor` (1 object: the `/v3dbuf` fd and size; 1 layer `DRM_FORMAT_NV12`; 2 planes `{offset 0, pitch width}` and `{H16×128, pitch width}`; `format_modifier = SAND128_COL_HEIGHT(C)`), and `frame->buf[0]` = an `AVBufferRef` whose free returns the BO to the pool.
- `libavutil/hwcontext_drm.h` is header-only for this use.

**The per-picture gate and the frame allocation.** The gate runs after the frame is allocated, which matters in this mode:
- The gate `ff_rpivid_hevc_picture_ok` runs in `decode_nal_unit` **after** `hevc_frame_start` has allocated `s->ref` (patch `files/rpivid/patches/1001`, hevcdec.c hunk at ~3100; `picture_ok` dereferences `s->ref->hwaccel_picture_private`, `RV/rpivid_hevc.c:735`, `:782-785`).
- With `alloc_frame`, a picture the gate refuses would already be a `DRM_PRIME` frame when the CPU decoder starts writing into it.
- Design option 1: call the gate before `hevc_frame_start`. Everything it reads (`s->ps.pps`, `s->pkt.nals`, `s->sh`) is parsed by then.
- Design option 2: swap a refused picture's frame for a planar `get_buffer2` frame, the same mechanism as the fallback DPB rewrite below.
- **As built (§7.3), option 2.** The fallback hand-over (`ff_rpivid_hevc_drm_to_cpu`) rewrites *every* picture the decoder holds. That includes the current one, which is in the DPB as `s->ref`, and the staged `s->output_frame`, which follows it. So the refused picture is a planar frame before the CPU decodes into it, and the gate stays where it is. This is sufficient because `hevc_decode_frame` hands `s->output_frame` to the caller only after the picture is decoded (`hevcdec.c`, `av_frame_move_ref(rframe, s->output_frame)` at the end).

**Cropping.** `av_frame_apply_cropping` on an `AV_PIX_FMT_FLAG_HWACCEL` frame subtracts `crop_right`/`crop_bottom` from `width`/`height` and leaves the data alone (`ffmpeg-6.1/libavutil/frame.c:1017-1023`). So the consumer sizes the image from `frame->width`/`height`, and any non-zero `crop_top`/`crop_left` becomes a texture-coordinate offset. At 1080p only `crop_bottom = 8` is used, which the size already covers.

**Lifetime falls out of reference counting.**
- The DPB (`HEVCFrame`) and every consumer copy (`av_frame_ref`, `av_frame_clone` in WebKit's layer buffer, `0030`:233) hold `buf[0]`.
- A BO goes back to the pool only when hevcdec has dropped it as a reference **and** the compositor has destroyed the layer buffer that showed it.
- This replaces `RPIVIDFrame.buf` ownership (`RV/rpivid_hevc.c:206-215`). The pool's own refcount (`pool_ref`/`pool_unref`) keeps the BOs alive past `rpivid_uninit`, as today.

**Fence before reuse.** When `pool_get` takes a recycled BO, call `DRM_IOCTL_V3D_WAIT_BO(handle, timeout)` before the block writes to it. The BO is exported, so the server answers for Mesa's connection too (G6). This covers a GPU job still sampling the picture, for example a shadow blit queued just before the layer buffer was destroyed. It costs one IPC, and the BO is normally already idle.

**`end_frame`.** In `drm_prime` mode it skips the de-tile (`RV/rpivid_hevc.c:1078-1088`) and its `civac`. `rpivid-stat` gets a `zc=1` field, and `sand=` should read ~0.

**Still needs a planar picture:**
- **CPU fallback mid-stream** (`rpivid_fallback`, patch `1001`). The CPU decoder references DPB pictures through `frame->data[]`. In `drm_prime` mode those are BOs. On fallback, rewrite each DPB `HEVCFrame` into a planar `get_buffer2` frame and de-tile into it (uncached: ~8–10 ms × DPB size, a one-off hitch of ~60 ms). Consumers keep their own references to the old DRM frames. Later pictures come out as `YUV420P`, so **consumers must handle both formats per frame.** WebKit already decides per frame (`0030`:1752). Fallback happens: the block's intermittent error was seen once in 3000 pictures ([M10-hevc-hwaccel.md](M10-hevc-hwaccel.md):78).
- **Readback for snapshots, canvas `drawImage`, and the check tool.** Export `rpivid_frame_to_planar(const AVFrame *drm, AVFrame *planar)` from the port's libavcodec. It reuses `rpivid_sand8_to_planar` with the new strides. `hevc-rpivid-check` keeps the `planar` mode as its bit-exactness oracle and gains a `-zc` arm that goes through the readback.

**Pool size.** The DPB (`sps_max_dec_pic_buffering`, 5–6 for typical x265) plus the consumer's frames in flight. WebKit holds one displayed picture and replaces it on the next `setDisplayBuffer` (`0030`:1783-1786), plus one picture in the compositor's committed state. Expect 8–10 BOs at 1080p, i.e. 25–31 MB, as today. The `rpivid: … %d buffers` line at uninit (`RV/rpivid_hevc.c:543-547`) reports the real number.

**Frame threads.** WebKit opens the decoder with `thread_count = WPE_PHOENIX_MEDIA_THREADS` (default 0, FFmpeg's choice; `0030`:1588). `alloc_frame` runs on the frame thread that calls `ff_get_buffer`, and the pool is already mutex-protected. Keep the hosttest run with 1 and 4 threads ([M10-hevc-hwaccel.md](M10-hevc-hwaccel.md):135).

### 4.2 Path B: SAND imported directly (recommended)

**WebKit** (a new patch `webkit-video/0032-…`, layered on 0030/0031 **after the MSE agent's changes land**; 0030 and 0031 are not edited here):

- `CoordinatedPlatformLayerBufferFFmpeg::supportsPixelFormat` gains `AV_PIX_FMT_DRM_PRIME`. `MediaPlayerPrivateFFmpeg` opens `hevc_rpivid` with `rpivid_out=drm_prime` when the display has `EGL_EXT_image_dma_buf_import_modifiers` and `GL_OES_EGL_image_external`, with an env kill-switch `WPE_PHOENIX_MEDIA_ZEROCOPY=0`.
- **Import.** For a `DRM_PRIME` frame, `paintToTextureMapper` (`0030`:298-330) builds the `EGLImage` directly with these attributes:
  - `EGL_LINUX_DMA_BUF_EXT`, `EGL_LINUX_DRM_FOURCC_EXT = NV12`;
  - per plane: fd, offset, pitch = width (bytes per row of a linear plane, as the uapi asks), and the modifier lo/hi;
  - `EGL_YUV_COLOR_SPACE_HINT_EXT` and `EGL_SAMPLE_RANGE_HINT_EXT` from `frame->colorspace` and `color_range` (today's `yuvToRgbMatrix` logic, `0030`:272-296, becomes attribute selection).
  - It binds the image to a `GL_TEXTURE_EXTERNAL_OES` texture and draws it with `TextureMapper::drawTextureExternalOES` (`WK/Source/WebCore/platform/graphics/texmap/TextureMapper.h:87`). Mesa's lowered sampler does the YUV→RGB.
  - WebKit's own `CoordinatedPlatformLayerBufferDMABuf` cannot be reused because it is compiled only with `USE_GBM` (`WK/Source/WebCore/platform/TextureMapper.cmake:112-119`, `CoordinatedPlatformLayerBufferDMABuf.cpp:29`). Its `importToTexture` (`:69-79`) is the model.
- **Cache per pool BO.** Keep an `EGLImage` and texture cache keyed by the BO's `/v3dbuf` id (from the descriptor's fd), owned by the player and destroyed on the compositing thread, so each pool BO is imported once. Re-importing per picture would cost a `BO_IMPORT` round trip plus a new 3 MB shadow (§2.3).
  - Per picture: one texture bind and the draw.
  - Memory: N × 3.1 MB of shadows on top of the N pool BOs, about 25–31 MB more at 1080p.
- **Skia (`skiaImage()`, `0030`:352-385).** It is used for canvas `drawImage` and snapshots, not for composition. Either wrap the external texture (`GrGLTextureInfo` with target `GL_TEXTURE_EXTERNAL_OES`) or fall back to `rpivid_frame_to_planar`. Start with the readback: it is rare and simple.
- **Counters.** `countPaint` keeps measuring paint time (`0030`:262-269); `upload_ms` should drop to the import/bind cost.

**Cost per 1080p picture:**
- CPU: SAND 6.3 → 0 ms; upload ~5 → <0.2 ms (bind + draw); plus one `WAIT_BO` IPC per recycled BO.
- GPU: + `v3d_sand8_blit` of both planes (read 3.1 MB, write 3.1 MB tiled) per draw. Estimated 1–2 ms at the V3D clock; checkpoint 1 measures it.
- Removed: the composite of three uploaded R8 textures. Added: one external NV12 sample (two lowered samplers).

**What runs once per picture vs per draw.** The direct external draw re-blits on every composite of an unchanged picture (§2.3). That is harmless at 60 fps, where each picture is composited about once, but wasteful for a paused video under an animating page. If `prof`/GPU counters show it, convert once per picture: draw the external texture into an RGBA texture from `BitmapTexturePool` and composite that with the existing RGB layer buffer. The BO can then be released after that one blit, at the cost of an extra 8.3 MB RGBA write per picture. This is measured, not assumed.

**Risks:**
- The SAND blit's addressing on our column height. Retired by checkpoint 1.
- The per-draw re-blit (above).
- Shadow memory.
- Colour-hint plumbing in Mesa's lowered external path (BT.709 vs 601, narrow vs full). Checked by the probe's pixel compare.
- GPU load: 60 fps now puts the SAND blit on a GPU that is also compositing the page with GPU raster.

### 4.3 Path A: CPU de-tile into a LINEAR BO plus EGLImage import

It uses the same pool BOs (or a second set of LINEAR picture BOs) and the same `DRM_PRIME` frame, with `DRM_FORMAT_YUV420`/`NV12` and `MOD_LINEAR`, imported as in §4.2.

**What changes:**
- `rpivid_sand8_to_planar` writes into an **uncached** BO. Shared BOs must be uncached (`v3da_bo.c:1080-1086`, `LDP/xf86drm_phoenix.c:1178`).
- Full-line NEON stores into Normal-NC memory are write-combined on the A72, so the 6.3 ms may hold, but that is unmeasured.
- The upload disappears.
- Mesa still makes a tiled shadow per draw (TFU, §2.2).

**Net effect:** saves ~5 ms CPU per picture and keeps 6.3 ms, so about 38 % of a core at 60 fps remains.

**Extra cost:** a second BO set (the de-tile output), or de-tiling in place is impossible (SAND → planar needs a separate destination). That means +N × 3.1 MB.

**Verdict:** worse than B on every axis except one: it does not depend on `v3d_sand8_blit`. Keep it as the fallback (`rpivid_out=drm_prime_linear`) if checkpoint 1 fails and the shader cannot be fixed.

### 4.4 Path C: the HVS scans SAND out as a plane (full-screen players only)

**What exists:**
- `rpi4-kms`'s plane backend drives the firmware `SET_PLANE` call. Its 60-byte value already carries `vc_image_type`, `num_planes`, `is_vu`, `color_encoding` and `planes[4]` (`sources/phoenix-rtos-devices/video/rpi4-kms/kms_backend.c:25-30`).
- Linux fkms maps `NV12` + `SAND128` to `VC_IMAGE_YUV_UV`, with the pitch field = the column height in lines (`vc4_firmware_kms.c:575-651`).
- Card0 already imports a `/v3dbuf` BO (`kms_proto.h:526-527`, `KMS_IMPORT_NS_V3DBUF`).

**What is missing:**
- `ADDFB2` accepts LINEAR only (`kms_proto.h:516-518`, `:545`).
- `SET_PLANE` sends only `XRGB`/`ARGB` (`kms_backend.c:207-218`, `:233`).
- Overlays are off unless `-o` is given (`kms_backend.c:178-194`).
- The firmware fetches nothing at or above 1 GiB (`kms_proto.h:512-515`), so the pool BOs need `V3DA_BO_LOWMEM` (proto 5) and the low-memory budget (`-L`) must cover ~30 MB.

**Change:**
- `rpi4-kms` accepts `NV12` + `SAND128_COL_HEIGHT(C)` in `ADDFB2` (2 planes, same BO), then fills `vc_image_type = VC_IMAGE_YUV_UV`, `pitch = C`, `planes[0..1]` and `color_encoding` (`VC_IMAGE_YUVINFO_CSC_ITUR_BT709` / `BT601`). Take the enum *values* from `vc_image_types.h`, not from line numbers.
- The player puts the frame on an overlay plane above the primary with an atomic or legacy `SetPlane`, scaled by the HVS.

**Cost:** 0 CPU and 0 GPU per picture. The HVS scales and converts.

**Scope:** full screen, or a fixed rectangle on the display, only. A Wayland window (labwc) or a `<video>` element composited inside a page cannot use it. A browser hole-punch through labwc is out of scope.

**Effort:** 1.5–2 days in `rpi4-kms` plus the player side (§6).

**Risks:**
- Firmware behaviour of `VC_IMAGE_YUV_UV` on this firmware.
- Low-memory budget.
- Plane z-order against SDL's primary.

### 4.5 Comparison at 1080p60 (per picture; 16.7 ms budget)

| | CPU decoder thread | CPU compositor thread | GPU | Change surface | Agent-days |
|---|---|---|---|---|---|
| today | 6.3 ms (SAND) | ~5 ms (upload) | YUV composite | — | — |
| **A** | ~6.3 ms (into uncached BO, unmeasured) | ~0.2 ms | TFU shadow + composite | rpivid, FFmpeg, WebKit 0032 | ~5 |
| **B** | **~0** (+ `WAIT_BO` IPC) | **~0.2 ms** | **SAND blit (1–2 ms est.)** + composite | rpivid, FFmpeg, WebKit 0032 | **~6** |
| **C** | ~0 | n/a (no compositor) | 0 | rpivid pool (shared with B), rpi4-kms, player KMS output | +3 on top of B's decoder part |

---

## 5. Verification (existing gates and metrics)

**Correctness:**
- `hevc-rpivid-check` stays bit-exact in `planar` mode.
- A new `-zc` arm (readback through `rpivid_frame_to_planar`) must give the same per-frame MD5s on `/usr/share/video-demo/rpivid-check` at level 1.
- On screen: the HDMI snapshots of B8 arm 12 (colours right, no green or magenta).

**Browser:**
- `b8.sh hevc` (1080p30) and `b8-stream.sh hevc-fmp4`.
- Add a **1080p60** arm with `B8_HEVC_CLIP=/usr/share/video-demo/rpivid-check/real-peertube-1080.mp4` (1080×1920, 59.94 fps, the stream behind today's numbers).
- Grade on:
  - `WPEB-MEDIA … stat`: `painted`/`presented` → ≥ 95 % at 60 fps (today ~35/60), `dropped`, `upload_ms`;
  - `rpivid-stat sand=` → ~0 with `zc=1`;
  - `scripts/prof-report.py` → no `rpivid_sand8_to_planar`, no `v3d_store_utile` from the compositing thread.

**Players:** `ffplay-stat` fps and drops (`video-play`), and `top` CPU.

**A/B:** `FFMPEG_RPIVID_ZEROCOPY=0` / `WPE_PHOENIX_MEDIA_ZEROCOPY=0` in the same build gives the control arm, so both arms run the same binary (an A/B of two builds would also compare two memory layouts).

---

## 6. The standalone players (ffplay / `video-play`, gtk-video)

**ffplay** renders through `SDL_Renderer`: `upload_texture` → `SDL_UpdateYUVTexture` (`ffmpeg-6.1/fftools/ffplay.c:906-921`) into SDL's GL textures. That is the same CPU upload and `v3d_store_utile` cost as WebKit's. SAND images are **external-only**, so they cannot be attached to SDL's `GL_TEXTURE_2D` textures (`SDL_GL_BindTexture` + `glEGLImageTargetTexture2DOES` fails for an external-only image).

Two ways to reuse the decoder side of §4.1:
1. **Path B in ffplay** (windowed on Wayland and full screen on KMSDRM, ~1.5 days). A small GL video output used when the decoder returns `DRM_PRIME`:
   - an `SDL_GL` context on the window instead of the renderer, for the video only;
   - one `samplerExternalOES` quad, with the same `EGLImage` cache keyed by BO.
   - The OSD and subtitles are not drawn in this mode (ffplay draws them with the renderer), so `video-play` turns it on only for HEVC on rpivid.
   - On KMSDRM SDL's EGL is a kmsro screen, so each cached import also creates a KMS import (§2.3; one-time).
2. **Path C for full screen from psh** (after `rpi4-kms` §4.4, ~1 day on the player side). Take SDL's DRM fd (`SDL_GetWindowWMInfo` → `SDL_SYSWM_KMSDRM`, `drm_fd`), import the `/v3dbuf` fd on card0, `drmModeAddFB2WithModifiers(NV12, SAND128_COL_HEIGHT(C))`, and `drmModeSetPlane` on an overlay above SDL's primary. This gives 0 CPU and 0 GPU.

**gtk-video** converts with `libswscale` to RGB and paints with cairo (`files/gtk-video/gtk-video.c:409-460`, `:1066-1080`). Zero copy there means a `GtkGLArea` path, which is a rewrite of its display (~2 days). It is not worth doing while 30 fps content plays fine. It keeps `rpivid_out=planar`.

Players are lower priority than the browser: 1080p30 already plays in real time in `video-play` ([M10-hevc-hwaccel.md](M10-hevc-hwaccel.md):67-76). The gain there is CPU, not frames.

---

## 7. Implementation order and checkpoints

| # | Step | Gate | Agent-days |
|---|---|---|---|
| 1 ✅ PASS (§7.2) | **Standalone Pi probe, before any WebKit work** (`tools/gpu-lane/sand-import/`): `CREATE_BO` + `MMAP_BO` + PA; fill the BO with one real picture decoded by the block through the `RV` code in the new single-buffer layout, or a synthetic SAND pattern; `drmPrimeHandleToFD`; EGL surfaceless import NV12 + `SAND128_COL_HEIGHT(C)`; draw the external texture into an FBO; read back; compare with `rpivid_sand8_to_planar` + a reference YUV→RGB. Time the blit with `glFinish` around 100 draws. Also: the import of the bare modifier (expected to misbehave, §2.3) and a LINEAR import (path A's cost). | pixel match (± rounding of the colour matrix); SAND blit ≤ 2 ms at 1080p; contiguity check passes for 10 BOs | 1 (+1 Pi cycle) |
| 2 ✅ (host; §7.3) | **Decoder:** single-buffer layout in `rpivid_geom`/`pool_get` (planar mode first: must stay bit-exact); the picture gate moved before `hevc_frame_start` (§4.1; as built, the fallback hands the current picture over instead); then the V3D-BO pool + contiguity guard + `WAIT_BO`, `rpivid_out=drm_prime` with `alloc_frame`, readback helper, DPB rewrite on fallback. Host side: hosttest mock (as built: the decoder's own buffers stand in for BOs, no libdrm shim), ASan, 1 and 4 threads, `--loop` bit-exact through the readback. | hosttest 39/39 + loop bit-exact; Pi `hevc-rpivid-check` level 1 bit-exact in both modes | 2 (+1 Pi cycle) |
| 3 | **WebKit 0032** on top of the post-MSE 0030/0031: DRM_PRIME layer buffer, EGLImage cache, external-OES draw, colour hints, readback for `skiaImage()`, kill-switch. | builds; `b8.sh hevc` 1080p30 painted ≥ 95 %, HDMI colours right | 1.5 (+ 1–2 WebKit relinks) |
| 4 | **60 fps gate:** the 1080p60 arm + `b8-stream.sh hevc-fmp4`, `prof`; A/B with the kill-switch. | painted ≥ 95 % at 60; `sand≈0`; no `v3d_store_utile` from compositing; CPU freed ≈ 1 core | 1 (+2 Pi cycles) |
| 5 | **Players:** ffplay GL video output (B) for `video-play`; then `rpi4-kms` NV12/SAND overlay + ffplay KMS plane output (C). | `ffplay-stat` fps/drops at 1080p60, `top` | 1.5 + 3 |
| 6 | **10-bit:** SAND30 → P030 import (`v3d_sand30_blit`), skipping both CPU passes for Main10. | `b8-stream.sh main10-cpu` arm painted/presented | 1 |

**The decision point is after step 1.** If the SAND blit is wrong or slow and cannot be fixed in a day, steps 2–4 continue with path A (LINEAR, `drm_prime_linear`). Nothing in steps 2–4 depends on which modifier is used, apart from the de-tile running on the CPU.

---

### 7.1 Checkpoint 1: how the probe is built and run

The probe is `tools/gpu-lane/sand-import/` in the coordination repo. It lives there and not in a port because it is a throwaway measurement: it links two ports' build outputs (`mesa_drm` gles, `video_player`'s FFmpeg) plus `libdrm_phoenix`, and it ships in no image.

**Build** (host, read-only use of `.buildroot`, writes only `OUT`):

```
OUT=<dir> tools/gpu-lane/sand-import/build.sh
```

**What it does without changing the port.** The decoder's picture pool is redirected into render-server BOs by link-time `--wrap` of three functions that `rpivid_hevc.o` calls in other objects: `rpivid_geom`, `rpivid_dma_alloc_cached` and `rpivid_dma_free`.
- The geometry becomes the single-buffer layout of §4.1.
- The luma allocation becomes a V3D BO, and the chroma "allocation" is the same BO at `H16 × 128`.
- The hwaccel's own CPU de-tile then reads that BO with the new strides. So a correct decoded frame proves that the block wrote this layout.

**Run** (the file must be 8-bit HEVC):

```
/bin/sand-import -n 5 -i 100 /usr/share/video-demo/rpivid-check/x265-1080p-tu-amp.mp4
```

`-synthetic` (or no file) tiles a test pattern on the CPU instead of decoding, which checks the Mesa side without the block.

**Output lines** (all tagged `ZC1`):

| Line | What it reports |
|---|---|
| `geom` | the layout |
| `decode` | which BO matched the decoded frame, contiguity, `pa_check` (each page's marker written through the BO mapping must read back through a `MAP_PHYSMEM` mapping of the PA the block is given; a BO that fails is never handed to the block), PA range, CPU de-tile time from the uncached BO |
| `egl` / `modifiers` | the extension and modifier lists Mesa reports |
| `yuv plane=Y` / `plane=CbCr` | R8 and GR88 SAND imports, read back and compared **exactly** with the CPU de-tile |
| `nv12 variant=…` | the NV12 import with the parameterised modifier and pitch = width (the design), with pitch = 128, and with the bare modifier and pitch = C; each compared with CPU BT.709 and BT.601 narrow-range references |
| `time` | ms per SAND import draw (v3d_sand8_blit of both planes + draw), per RGBA draw of the same size, the difference (≈ the blit), today's 3-plane upload + draw, a LINEAR YUV420 import draw (path A), the CPU write of a planar picture into an uncached BO |
| `result=PASS\|FAIL` | the verdict |

**PASS means** all of the following:
- the decoded frame matches a BO;
- every BO is contiguous;
- both plane imports are bit-exact;
- the NV12 import is within 4 of the BT.709 reference;
- the SAND import draw ran.

Only the measured decoder's pool goes into BOs: the wrap is switched on after `avformat_find_stream_info`. A `decode result=FAIL` (for example `pa_check=FAIL`) runs the GPU arms on the synthetic pattern. That is still a valid Mesa result, but not a block-into-BO result.

**Go/no-go** (§7 step 1):
- PASS and `sand_blit_est_ms` ≤ 2 → path B;
- an exact-plane FAIL that the variants do not fix → path A.

### 7.2 Checkpoint 1 result (build 58, `artifacts/rpi4b-uart/rpi4b-uart-20261007-200402-zc1.log`)

`ZC1 result=PASS`, both with the decoder and with `-synthetic`.

**Decode into BOs.**
- `hevc_rpivid` decoded into 5 render-server BOs: `contiguous=5`, `pa_check=PASS`, PA `0x27000000..0x2aafd000`.
- Geometry: `cols=15 colh=1632 chroma_off=139264 stride=208896`.
- The decoded frame matched a BO. So the block writes the single-buffer NV12_COL128 layout, and the hwaccel's own de-tile reads it.

**Mesa import.**
- Y (R8) and CbCr (GR88) SAND128 imports: `max_diff=0`, i.e. `v3d_sand8_blit` is bit-exact on our geometry.
- NV12 import, all three variants (column-height parameter with pitch = width or pitch = 128; bare modifier with pitch = C): `max_diff_709=1`, `over4=0`.
- So the BT.709 hint is honoured, and the uapi's pitch = width convention works.

**Time per 1080p picture:**

| Measurement | ms |
|---|---|
| SAND import draw (sand8 blit of both planes + draw) | 8.68 |
| RGBA draw of the same size | 4.76 |
| ⇒ SAND blit (difference) | **3.92** |
| 3-plane upload + draw (today's browser path) | 8.84 |
| LINEAR YUV420 import draw (path A) | 8.06 |
| CPU write of a planar picture into an uncached BO | 3.57 |
| CPU de-tile from an uncached BO (synthetic run: 16.51) | 8.60 |

**Decision (coordinator): path B.** The blit is 3.9 ms, over the 2 ms bar of §7.1, but it is GPU time and it removes ~11 ms of CPU per picture (6.3 ms SAND + ~5 ms upload).

The upload row (8.84 ms) is the CPU-side cost B removes. The SAND import draw costs about the same wall time, but on the GPU. If the GPU becomes the bottleneck at 60 fps:
- blit once per picture into RGBA (§4.2);
- or move the conversion to the TFU's SAND input (§2.3).

### 7.3 Step 2 landed: the decoder side (ports branch `rpivid-drm-prime`, `2295fd5`)

**What it is** (`video_player/files/rpivid/`):

- **`src/rpivid_drm.h`** (new, installed as `libavcodec/rpivid_drm.h`) is the consumer interface:
  - the option `rpivid_out=planar|drm_prime` (env `FFMPEG_RPIVID_OUT`);
  - the frame format (one object, one NV12 layer, pitch = coded width, `SAND128_COL_HEIGHT(C)`);
  - `rpivid_drm_set_buffer_ops()`, where the picture buffers come from;
  - `rpivid_drm_frame_to_planar()`, the CPU readback with the conformance window applied.
- **`src/rpivid_cmd.[ch]`:** `rpivid_geom_col128()`, the single-buffer layout. `rpivid_geom()` is unchanged.
- **`src/rpivid_hevc.c`:**
  - A second hwaccel, `hevc_rpivid_drm` (`AV_PIX_FMT_DRM_PRIME`), with `alloc_frame`: the frame **is** a pool buffer, wrapped in an `AVBufferRef`. A buffer goes back to the pool when the last reference (DPB, consumer, `RPIVIDFrame`) is dropped. On reuse, `wait_idle()` (`DRM_IOCTL_V3D_WAIT_BO`) runs before the block writes it.
  - `end_frame` skips the de-tile; `rpivid-stat` gets ` zc=1`.
  - It is taken only for 8-bit streams without frame threading; otherwise the frames are system-memory frames, as before.
  - `ff_rpivid_hevc_drm_to_cpu()` runs on a fallback. Every picture the decoder holds (DPB, the current picture, the staged output) becomes a system-memory frame with the block's pixels, so the CPU decoder can continue. `avctx->pix_fmt` returns to the software format, and consumers see `YUV420P` frames from then on.
  - `FFMPEG_RPIVID_REFUSE_AT=n` makes the block refuse a picture, to test the fallback.
- **`patches/1001`:**
  - `get_format()` answers `AV_PIX_FMT_DRM_PRIME` when the drm hwaccel attached.
  - `rpivid_fallback()` hands the pictures over and is now error-checked.
  - The SEI picture hash is not checked on DRM frames.
  - New options `rpivid_out`; `rpivid_drm.h` is installed.
- **`drm/rpivid_bo_drm.[ch]`** (new): the buffer operations on the V3D render server.
  - Allocation: `CREATE_BO`, `MMAP_BO` + `mmap`, the PA by `va2pa` (refused unless contiguous), and `drmPrimeHandleToFD` → `/v3dbuf/<h>`.
  - Reuse waits with `WAIT_BO`.
  - The port installs it as `ffmpeg/lib/librpivid_bo_drm.a` + `ffmpeg/include/rpivid_bo_drm.h` for webkit_wpe. A program linking it adds libdrm-phoenix's `libdrm.a` and its five `--wrap` flags.
- **`check/hevc-rpivid-check.c`:**
  - `-zc` decodes with `rpivid_out=drm_prime` on GPU buffers (the Pi build links `rpivid_bo_drm.o` + libdrm) and hashes each DRM frame through the readback.
  - `-hold n` (default 4) keeps the last n frames referenced, as a compositor does, and re-hashes each when it is released. A buffer reused while held shows as `held_bad=` (counted as mismatches).
  - New line: `RPIVID-CHECK zc frames= drm_prime= buffers=gpu|own held_checked= held_bad=`.
- **`hosttest/run.sh --loop`:** per 8-bit clip, `-zc` with 1 and 4 (slice) threads must be BIT-EXACT with `held_bad=0`. A forced fallback (`FFMPEG_RPIVID_REFUSE_AT=3`) with `-zc` must give exactly the frames of the planar output.

**Host evidence** (x86, ASan, the register-level mock):
- The default run gives `same=39 diff=0 cpu=1`, identical to the baseline before the change. Planar programming is unchanged.
- `--loop` on 7 host-encoded clips from the `rpivid-check` set (`x265-amp`, `-medium`, `-slices4`, `-1080p-tu-amp`, `-10bit-tu`, `-ultrafast`, `-ctu32`). For every 8-bit clip:
  - `-zc` with 1 and with 4 (slice) threads is **BIT-EXACT**: `drm_prime=120 held_checked=120 held_bad=0`;
  - the forced fallback (`FFMPEG_RPIVID_REFUSE_AT=3`) gives **the same 120 frames as planar output**.
  - The 10-bit clip stays planar (BIT-EXACT as before). The planar loop arms are unchanged and BIT-EXACT.
- On the host the buffers are `buffers=own` (the decoder's own memory). The GPU-buffer path in `rpivid_bo_drm.c` (`CREATE_BO`, the `va2pa` contiguity check, the `/v3dbuf` export, `WAIT_BO` on reuse) runs **only on the Pi** (checks 2 and 5 below).

**The CPU fallback (P31, closed).** Continuing a stream on the CPU right after the block refused or failed a picture was **not bit-exact** until the next IRAP: on the mock, 28 of 120 frames differed from a whole-stream CPU decode, in both outputs.
- The likely cause: the CPU decoder reads the motion vectors of block-decoded reference pictures for TMVP (`tab_mvf`), and the hwaccel never fills them.
- This was not new: the Pi's `fallback=1` runs only ever counted frames, never checked them.
- **Decision (coordinator) and fix** (ports branch `webkit-zero-copy`, `video_player` commit):
  - On a refusal or block failure the decoder drops the current picture (unless it is an IRAP the CPU can decode whole) and every picture up to the next IRAP. The RASL pictures of a CRA are dropped too (`max_ra`). The player keeps showing its last picture.
  - It logs `rpivid: continuing on the CPU decoder (…): pictures dropped until the next random access point` and then `rpivid: decoding again from POC n (a random access point)`.
  - `hevc-rpivid-check -bypts` compares the passes by pts and counts `matched=` / `wrong=` / `dropped=`.
  - Host: `--loop` arms `LOOP-DROP` force a refusal (`FFMPEG_RPIVID_REFUSE_AT=3`) and a block failure (`MOCK_FAIL_AT=5`) in both outputs. Result: `wrong=0 unmatched=0` with 26–57 pictures dropped per 120 (to the next IDR).

**For the WebKit patch 0032.**
- `drm_prime` is refused under frame threading. WebKit opens the decoder with `thread_count` 0 (auto, i.e. frame threads), and then logs `rpivid: drm_prime output needs no frame threading` and gets planar frames. 0032 must set `thread_type = FF_THREAD_SLICE` (or `thread_count = 1`) together with `rpivid_out=drm_prime`.
- It calls `rpivid_bo_drm_install()` once and links `ffmpeg/lib/librpivid_bo_drm.a`. WebKit already has libdrm-phoenix and its five `--wrap` flags.
- It must handle `DRM_PRIME` and `YUV420P` frames per frame (a fallback switches mid-stream).

**Not done: ffplay/`video-play` GL output.** It is not cheap. ffplay draws through `SDL_Renderer`, and SAND/NV12 images are external-only, so they cannot be attached to SDL's `GL_TEXTURE_2D` textures. It needs its own GL video output (an SDL GL context, a `samplerExternalOES` quad, the EGLImage cache), about 1.5 days, or the KMS plane path (§4.4). ffplay keeps `planar`; nothing changes for it, or for gtk-video and WebKit, until they ask for `drm_prime`.

**Pi checks** (after building `video_player`; the new `/usr/bin/hevc-rpivid-check` on the NFS root):

| # | psh command | Pass |
|---|---|---|
| 1 | `hevc-rpivid-check -q -l 2 /usr/share/video-demo/rpivid-check` | planar unchanged: the same pass/fail list as the last full run |
| 2 | `hevc-rpivid-check -zc /usr/share/video-demo/rpivid-check/x265-1080p-tu-amp.mp4` | `zc … drm_prime=120 buffers=gpu held_checked=120 held_bad=0`, `result=PASS`; `rpivid: drm_prime output: NV12 SAND128 column height 1632 … GPU buffers (dma-buf)` |
| 3 | `hevc-rpivid-check -zc -q -l 2 /usr/share/video-demo/rpivid-check` | every stream that passes in #1 passes here. The 10-bit ones log `drm_prime output is 8-bit only` and pass with `drm_prime=0`. |
| 4 | `export FFMPEG_RPIVID_REFUSE_AT=30` then `hevc-rpivid-check -hw -md5 -zc <clip>` and the same without `-zc` | the two `MD5` lists are identical; `continuing on the CPU decoder`; no crash. Then `export FFMPEG_RPIVID_REFUSE_AT=` to clear it. |
| 5 | `hevc-rpivid-check -zc -hold 8 /usr/share/video-demo/rpivid-check/x265-1080p-tu-amp.mp4` | `held_bad=0 buffers=gpu`; the `rpivid: … N buffers` line at the end ≈ the DPB + 8. This holds frames longer than the DPB alone, so recycled buffers go through `WAIT_BO` while a consumer still holds others: the one path the host cannot reach. |

- `-zc` reads every picture back from uncached memory (~8.6 ms at 1080p), so its fps is not the zero-copy speed.
- The speed is the WebKit gate (step 4).

### 7.4 Step 3: the WebKit side (ports branch `webkit-zero-copy`)

**Patches.**
- `webkit_wpe/patches/webkit-video/0033-wpe-phoenix-ffmpeg-zero-copy.patch` (USE video, after 0030/0031) changes `CoordinatedPlatformLayerBufferFFmpeg.{h,cpp}` and `MediaPlayerPrivateFFmpeg.{h,cpp}`.
- `webkit-mse/0034-wpe-phoenix-ffmpeg-media-source-zero-copy.patch` (USE mse, after 0032) changes `FFmpegPlaybackEngine.{h,cpp}` and `MediaPlayerPrivateFFmpegMSE.cpp`.
- Both are BSD-2-Clause like 0030. They are split because 0032 exists only with USE mse; 0032 touches none of 0033's files, so the order 0030, 0031, 0033, 0032, 0034 applies cleanly (checked with `patch --dry-run` on the 0031 tree).
- `files/build-wpe.sh` copies `librpivid_bo_drm.a` and `rpivid_bo_drm.h` from the FFmpeg prefix into the dependency view. Its archives all join the link group, and libdrm and the five `--wrap` flags are already there.

**Decoder.**
- `CoordinatedPlatformLayerBufferFFmpeg::requestZeroCopy()` runs before `avcodec_open2` of a video decoder, in both players.
- Only for `hevc_rpivid`, unless `WPE_PHOENIX_MEDIA_ZERO_COPY=0`. Once per process it checks `EGL_EXT_image_dma_buf_import_modifiers` and calls `rpivid_bo_drm_install()`, logging `WPEB-MEDIA zero-copy available|unavailable: <why>`.
- It sets `rpivid_out=drm_prime` and `thread_type = FF_THREAD_SLICE`.
- The EGL check is safe on the player thread: the WebProcess sets its shared `PlatformDisplay` eagerly, at process initialisation (`WebProcessGLib.cpp`, `setSharedDisplay`), before any page or player exists.
- A `DRM_PRIME` frame of another layout than hevc_rpivid's NV12 is refused when the layer buffer is created (logged once).
- The `decoder …` line adds `zero_copy=0|1`.

**Compositor.**
- A `DRM_PRIME` frame is drawn with `TextureMapper::drawTextureExternalOES` from a per-compositing-thread cache of EGLImage + `GL_TEXTURE_EXTERNAL_OES` texture, one per decoder buffer.
- The cache key is the dma-buf's `st_dev`/`st_ino`, i.e. the render server's BO handle, which is never reused. One `fstat` per newly painted picture; a frame repainted reuses its layer buffer's result.
- The EGL attributes carry the frame's colour space and range as hints. Entries idle for 3 s are dropped, at most 48.
- The layer buffer holds the `AVFrame` reference until the compositor drops it, and the decoder's `WAIT_BO` covers GPU reads still in flight.
- Ordinary frames (CPU fallback, 10-bit, H.264, other codecs) take the upload path as before, frame by frame. An import failure (logged once) and Skia painting (canvas `drawImage`, snapshots) use the CPU readback `rpivid_drm_frame_to_planar` (~8.6 ms at 1080p, uncached).

**Stat lines** (both players) add `zc=1|0` (the last picture presented was a GPU buffer) and `zc_painted=N`. `upload_ms` stays the compositor's CPU time per painted picture: for zero copy, the cache lookup (or the one-time import) plus the draw call.

**Compile.**
- The scratch WebKit build reused the MSE agent's incremental tree (`tools/browser/wpe/build.sh --src-copy`, under `scripts/heavy-build.sh`). It used the ports `webkit-zero-copy` `files/build-wpe.sh`, and a private FFmpeg prefix with `libavcodec` cross-built from the branch's `video_player` sources plus `rpivid_drm.h`, `librpivid_bo_drm.a` and `rpivid_bo_drm.h`.
- Ninja ran 227/227 steps (128 C++ compiles: every unit that includes the FFmpeg headers) with no warning in the four changed files. `wpe-browser` is 133,890,800 bytes stripped, and the build's symbol checks passed.
- `nm`: `rpivid_bo_drm_install`, `rpivid_drm_frame_to_planar`, `rpivid_drm_set_buffer_ops`, `drmPrimeHandleToFD`, `__wrap_mmap` and `ff_hevc_rpivid_decoder` are present.
- `strings`: `WPEB-MEDIA zero-copy` ×5, the new stat format ×2, and the decoder's `drm_prime output` / `pictures dropped …` lines. So the zero-copy path is compiled, not the fallback.
- The patch series 0030, 0031, 0033, 0032, 0034 applies in the port's order and reproduces the tree.

**Pi gate plan.** Build `video_player` (≥ ports `b0b0fcc`) **before** `webkit_wpe`: build-wpe.sh takes `librpivid_bo_drm.a` from the FFmpeg prefix at its deps stage. A stale video_player install only warns (`has no librpivid_bo_drm.a: <video> without zero copy`) and yields a browser without zero copy. Check that `strings` of the staged wpe-browser contains `WPEB-MEDIA zero-copy`.

| # | Run | Grade |
|---|---|---|
| 1 | `b8.sh hevc` (the 1080p30 clip), twice: as is, and after `export WPE_PHOENIX_MEDIA_ZERO_COPY=0` | ZC on: `WPEB-MEDIA zero-copy available`, `decoder video=hevc_rpivid … zero_copy=1`, `rpivid: drm_prime output: … GPU buffers (dma-buf)`, stat `zc=1`, `zc_painted` ≈ `painted`, `upload_ms` ≪ the OFF arm's, `rpivid-stat … sand=0.00 … zc=1`; painted ≥ 95 % of presented; HDMI colours right. ZC off: `zc=0`, today's numbers. |
| 2 | the same arm on the PeerTube 1080p60 file: `export B8_HEVC_CLIP=/usr/share/video-demo/rpivid-check/real-peertube-1080.mp4`. **OFF first** (`WPE_PHOENIX_MEDIA_ZERO_COPY=0`), then ON, **the same build**, each with `prof` | Record OFF's painted/presented per second: build 56–57 painted ~35 of 60, before the row-order fix. ON must beat OFF by about the ~11 ms of CPU per picture removed, aiming at painted ≥ 57/s. `scripts/prof-report.py`: no `rpivid_sand8_to_planar`, no `v3d_store_utile` on the compositing thread; the WebProcess CPU ~1 core lower. |
| 3 | `b8-stream.sh hevc-fmp4` (native HLS → the progressive player), ZC on and off | `B8S arm=hevc-fmp4 … hw=1`, `zc=1`, dropped ≤ 2 %, painted ≥ 95 % |
| 4 | the MSE gate arms (MSE-DESIGN.md §10.2) with an HEVC stream, ZC on | MSE stat line `zc=1`, the same pass criteria as without ZC |
| 5 | (robustness) the 1080p60 arm with `export FFMPEG_RPIVID_REFUSE_AT=300` | after picture 300: `pictures dropped until the next random access point`, then `decoding again from POC`; `zc=0` from then on (CPU frames); no crash, no green/garbage frame on HDMI |

### 7.5 Build 60 on the Pi, and round 2

**Build 60 gate** (coordinator; `artifacts/rpi4b-uart/rpi4b-uart-20261007-232326-zcgate.log`, `prof-zc0` and `prof-zc1`). PeerTube 1080×1920 60 fps, `b8.sh hevc`, GPU raster + dma-buf:

| | painted/s | UI present | `upload_ms` |
|---|---|---|---|
| OFF | ~33 | 31–38 fps | 6.2 |
| ON | ~28 | 28 fps | 17 |

- **ON:** `zero-copy available (rc=0)`, `zero_copy=1`, `rpivid: drm_prime output: NV12 SAND128 column height 2880 … GPU buffers (dma-buf)`, `rpivid-stat … sand=0.01ms zc=1`, stat `zc=1` but **`zc_painted=0`**.
- The compositing thread spent 66 % of a CPU in the CPU readback (`vld2q_u8`/`vld1q_u8_x4` = `rpivid_sand8_to_planar` from uncached memory) plus `v3d_store_utile`.
- **Cause.** WPE's default composition is Skia: `SkiaCompositingLayer` draws a contents buffer through `skiaImage()` (`SkiaCompositingLayer.cpp:795`), not `paintToTextureMapper`. 0033 imported the buffer only in `paintToTextureMapper`, and its `skiaImage()` read every picture back.
- The decoder half worked as designed: the block wrote the GPU buffers, `sand=0.01ms`.
- **Refusal arm:** the drop to the IRAP worked, but the stream then stayed on the CPU decoder (~6 fps at 1080p60).

**Round 2** (ports branch `webkit-zero-copy`):
- **0033:**
  - `skiaImage()` wraps the buffer's `GL_TEXTURE_EXTERNAL_OES` texture as an `SkImage` (`SkImages::BorrowTextureFrom`, WebKit's own `CoordinatedPlatformLayerBufferExternalOES` pattern). Skia's texture-binding state is reset after an import.
  - Every draw touches the cache entry, so it cannot be dropped while shown.
  - One `fstat` per picture: the layer buffer keeps its cache key.
  - One line per process names the path each compositor takes: `WPEB-MEDIA zero-copy path=external-oes|readback compositor=skia|texturemapper (first picture WxH)`. Every reason a frame is not imported is logged once: no dma-buf, crop, `fstat`, EGL import (with fourcc/modifier/pitch/offset), GL texture, Skia wrap.
- **video_player:**
  - At the IRAP where decoding resumes, or at the next IRAP after an IRAP the CPU decoded, the block is attached again with the same output. This happens at most `FFMPEG_RPIVID_RETRIES` times per stream (default 3; 0 = never), and not under frame threads.
  - A retake CRA's RASL pictures are dropped. Logged: `rpivid: back on the block from POC n (retry i of m)` or `the block stays off for this stream (…)`.
  - `FFMPEG_RPIVID_REFUSE_AT` now refuses once per process.
- **Host test** (`--loop`, 5 8-bit clips + 1 10-bit) runs refusal, block failure and refusal with retries 0, in both outputs:
  - every arm `wrong=0 unmatched=0`;
  - `retakes=1` (0 with retries 0), with the block decoding the rest of the stream after the retake (`89 pictures on the block` of 120);
  - the default output still `same=39`.
  - The mock takes `MOCK_GOLDEN_IDR` (the IDRs' display indices), since it does not see dropped pictures.
- **Compile:**
  - Scratch build in the MSE scratch tree's `out/`, from my own source tree (ports `1f1f818` patches + this round), under `heavy-build.sh`.
  - Ninja ran 227/227 steps, with no warning or error in the changed files; the symbol checks passed. `wpe-browser` is 133,895,216 bytes stripped.
  - The binary has the path and reason log strings, `back on the block from POC` and the new stat format.
  - The patch series 0030, 0031, 0033, 0032, 0034 reproduces the tree.

**Pi gate (round 2)**, with `WPE_PHOENIX_MEDIA_ZERO_COPY=1` for ON:
- The log must show `zero-copy path=external-oes compositor=skia` and `zc_painted` ≈ `painted`. If it shows `path=readback`, the reason line before it names the cause.
- Then the §7.4 table rows 1–4, OFF first.
- Row 5 (`FFMPEG_RPIVID_REFUSE_AT=300`): `back on the block from POC n` at the next IDR, and the fps recovering to 60.

### 7.6 Build 62/63 on the Pi, and the block's lost completion (round 3)

**Build 62** (coordinator), PeerTube 1080p60, zero copy OFF → ON:

| | OFF | ON |
|---|---|---|
| painted/s | 33 | 49 |
| `upload_ms` | 6.2 | 0.21 |
| dropped | 18 | 0 |
| web process CPU | 34 % | 16 % |

- ON logged `path=external-oes compositor=skia`.
- The refusal arm logged `back on the block (retry 1 of 3)`.
- Zero copy was then made the default (ports `75686da`). After build 63 the coordinator made it opt-in again until §7.7 is checked on the Pi.

**Build 63 gate** (`rpi4b-uart-20261008-010124-b63-gate.log`): the MSE arm had `zero_copy=1` but `zc_painted=0`. It is **not** a separate MSE paint path:
- `FFmpegPlaybackEngine::presentFrame` hands its frames to the same `CoordinatedPlatformLayerBufferFFmpeg` and the same contents-buffer proxy, so they reach the same `skiaImage()`.
- The MSE process never got a DRM frame. Its first picture timed out on the block (`the block failed picture POC 0: timeout (CFSTATUS 256 CFNUM 256 …)`, a phase-2 timeout), so the stream went to the CPU (`stat … hw=0 zc=0`).
- The block was already wedged by the HLS arm before it, in another process. That arm ended with `the block failed picture POC 6: timeout` after 1388 pictures, and a block that stops responding stays stopped for every later process.

**The cause of the wedge: a lost interrupt race in `rpivid_hw.c` (pre-existing, not zero copy).**
- In that HLS arm, `rpivid-stat` phase 2 fell from 3.3 ms to **0.02 ms per picture** after ~400 pictures and stayed there until the timeout. A 1080p reconstruction cannot finish in 20 µs.
- The same collapse appears in earlier runs without zero copy (`b56-gate`, `v60prof`: `p2 0.02` with `sand=6.8–12 ms`) and in `zcseq`.
- The mechanism:
  - The interrupt handler and the waiter (which also polls the interrupt controller) can both observe one completion.
  - When the handler's `irq_active |= bit` lands after the waiter consumed the completion through its own read, a stale "phase 2 done" mark remains.
  - The next picture's phase-2 wait then returns at once, before the block finished. Its output is read (planar) or shown (zero copy) while still being written, and the next picture's phase 1 starts on a busy block.
  - From then on every wait is one completion behind (hence the constant 0.02 ms), until the block times out and stays wedged.
- The phase-1 form of the same race explains the block's "known intermittent decode error": the wait returns early, `CFSTATUS < CFNUM` is read, and it is treated as a decode error (M10-hevc-hwaccel.md, `CFSTATUS 71 CFNUM 264`).

**Fix** (ports branch `webkit-zero-copy`, `video_player`, `a8441e4`):
- `rpivid_hw_decode` clears a completion of the phase that is already pending just before it starts that phase (`clear_stale`, counted).
- `wait_active` clears the handler's mark when it consumed the completion itself.
- `irq_active` is updated atomically on both sides.
- `rpivid-stat` adds `stale=N`, the uninit line `N stale completions`. A slow block wait (> 100 ms) is logged as `rpivid: picture POC n took X ms on the block`, and a slow or failing `WAIT_BO` as `rpivid-bo: WAIT_BO handle h: … after X ms`, so a stall like build 63's second session names its cause.

**Host evidence.**
- The mock gains `MOCK_ISR_RACE=1`: a registered handler that runs inside the waiter's controller read whenever that read sees a completion, i.e. the race. The interrupt path had never been host-tested.
- With the fix: BIT-EXACT, `completion by interrupt`, `0 stale completions` on every clip.
- With the waiter's clear removed (the old waiter): `238 stale completions` in 120 pictures. They are all caught by the pre-start clear, so still BIT-EXACT.
- Default run `same=39`; every `--loop` arm (zero copy, drop, retake) still passes.

**Teardown questions** (coordinator):
- A zero-copy player leaves nothing that blocks another process's import:
  - Its picture buffers are released with the last frame reference (the decoder's pool, the compositor's layer buffer). The import cache's entries are dropped 3 s after their last draw, and in any case at process exit, when the render server releases a dead client's BOs.
  - `WAIT_BO` is only ever called by the decoder on its own buffers, bounded to 5 × 2 s, and now logged when slow.
  - The readback holds no buffer beyond its frame.
- What does cross processes is the **wedged block**: the next process's first picture waits 1–2 s for it, then decodes on the CPU (`hw=0`). Round 4 (§7.7) stops later processes from touching it at all.
- The stall with no log lines is examined in §7.7.

**Pi check (round 3):**
- Any long HEVC run (PeerTube 1080p60, the HLS ladder): `rpivid-stat … p2 ≈ 3 ms` throughout (never ~0.02), `stale=0` or small with no lasting effect, no `took … ms on the block`.
- Then the build 63 sequence (HLS session + MSE session, then the demo): MSE `zc_painted` ≈ `painted` with `hw=1`.

### 7.7 The block's DMA reach, and a block that stops responding (round 4)

The coordinator's hypothesis was that the drm_prime picture buffers sit where the block cannot DMA. KMS lines in build 63 show v3dbuf BOs at `pa0=0xf0400000` (`why=above_1g`), while checkpoint 1's probe buffer was at 0x27000000.

**The block reaches all of a Pi 4's RAM. The hypothesis is ruled out.**
- In the Raspberry Pi device tree (`bcm2711-rpi-ds.dtsi`), `hevc_dec` is a child of `scb`. The `scb` `dma-ranges` map bus addresses 1:1 onto the first 16 GiB.
- The Linux `hevc_dec` driver sets a 36-bit DMA mask. It writes addresses as `pa >> 6` into 32-bit registers, as `RPI_VC_ADDR` does, which covers 256 GiB.
- A Pi 4's RAM ends at 8 GiB.
- The build 63 logs agree. The HLS arm decoded 1388 pictures zero-copy before its timeout, and the reruns decoded 1800, 1800 and 1362 into buffers from the same allocator. An unreachable buffer would fail on the first picture every time.
- The `pa0=0xf0400000 … why=above_1g` line is not a picture buffer. It is KMS importing WebKit's own 1000x620 AB24 swap-chain buffer (625 pages): the HVS cannot scan out above 1 GiB, so KMS composes it. A picture buffer is 765 pages (3133440 bytes at 1080p).
- So the pool is **not** moved below 1 GiB (`DRM_PHOENIX_V3D_CREATE_BO_SCANOUT`). That memory is the KMS scanout budget, and the block does not need it.

Two changes were still made:
- Every picture buffer's placement is logged: `rpivid-bo: BO handle h: PA a-b (n bytes)`. A stall or corruption report can be matched against these lines.
- A guard, `RPIVID_DMA_LIMIT` (16 GiB), is checked for the GPU buffers and for the decoder's own contiguous memory.
  - At attach, the decoder takes the first drm_prime buffer at once. If it cannot get one, or the buffer is out of reach, the stream keeps system-memory frames and is still decoded by the block: `rpivid: drm_prime output: <why>: system-memory frames for this stream`.
  - A later buffer that fails logs `rpivid: no buffer for another picture (n held): <why>`.

**What the build 63 timeouts show.**
- Both timeouts (HLS `POC 6`, MSE `POC 0`) report `CFSTATUS == CFNUM`. Those registers were read only after phase 1 completed, so **phase 2 hung** both times, and phase 1 still ran on the stuck block.
- In the HLS process the hang followed ~800 pictures of `p2 0.02` (§7.6's race). The MSE process was given a block that process 1 had left in the middle of phase 2.
- The MSE process also read the block's registers after its timeout. So register reads of a stuck block return; they do not hang the bus.
- The demo process (the third) printed `drm_prime output` and then nothing until the capture ended: no 2 s `stat` line, and no `the block failed` line (that would follow 1–3 s of waiting). So it stopped **outside** the bounded block waits.
  - Candidates are the render-server calls that allocate its picture buffers (no timeout on Phoenix) or, from the log alone, the whole system.
  - One more fact points at the system: process 2 had restarted the stuck block with a new picture. If the stuck job then resumed, it wrote into picture buffers that were freed when process 1 exited, and the memory may already have been reused.
  - The new `rpivid-bo:` line per buffer and the `rpivid: first picture on the block` line bracket where the next occurrence stops.

**Containment: a block that stops responding is never touched again in this boot** (ports branch `webkit-zero-copy`, `c23645a`, `rpivid_hw.c`):
- **On a phase timeout**, in the process that saw it:
  - It reads CFSTATUS, CFNUM and STATUS for the log, so a phase-1 timeout now reports real values too.
  - It records the state in `/tmp/.rpivid.dead`: `wedged pid=<pid> t=<ms since boot> phase N timed out (…)`. `/tmp` is the RAM file system made at each boot (`user.plo.yaml`). A marker whose time is later than the current boot time is a previous boot's, and is removed.
  - It switches the HEVC clock off through the mailbox. A stuck block then cannot finish a transfer into memory freed later. Linux gates the same clock whenever the block is idle (runtime PM). Before that it clears any pending completion and detaches the interrupt handler, so nothing reads the block's controller without a clock.
  - Gating the clock on a timeout is the one action new on the hardware in this round: the mock cannot prove it, and a timeout cannot be provoked on the Pi. The first real timeout tests it. Its log line ends in `(clock switched off)` or `(clock left on: the mailbox refused)`, and the system must stay alive after it.
  - It does not free any buffer the block was given (command, PU and coefficient buffers, and pictures, including GPU buffers). They stay allocated until the process exits; this happens at most once per boot.
  - The log line is `rpivid: the block failed picture POC p: timeout in phase N (…); the block is not used again until reboot (clock switched off)`.
- **Every later open**, in any process, fails at once, before any mailbox call or register access: `rpivid: CPU decode: the block stopped responding earlier in this boot and is not used again (/tmp/.rpivid.dead: …)`. The video plays on the CPU (`hw=0`) with no wait.
- **`FFMPEG_RPIVID_RESET=1`** allows one try per boot:
  - It records `reset-tried` first, then switches the clock off and on. The first picture is the probe, bounded by the 1 s and 2 s phase waits.
  - On success the marker is removed and the log says `first picture on the block: … (the block works again after the clock reset)`.
  - On failure, or if the trying process dies, `reset-tried` stays and nobody tries again.
  - **It is off by default.** Nothing shows that a clock off/on resets the block: the Linux driver has no reset, and the firmware has no HEVC power domain. Restarting a stuck block is also the one action that preceded the silent stop above.
- **Waits:**
  - All block waits are bounded: the lock is `F_SETLK` (never waits), phase 1 waits at most 1 s, phase 2 at most 2 s, and `WAIT_BO` at most 5 × 2 s.
  - The mailbox server bounds its own spins (`rpi4-vcmbox` `MBOX_SPINS` and retries).
  - Not boundable from user space: a `msgSend` to the mailbox server or the render server has no timeout on Phoenix, so a hung server hangs its caller.
  - A register read of a hung block has no software bound either. The Pi showed such reads returning, and after the marker none are made.

**Host evidence** (`hosttest/run.sh --loop`, on the first 8-bit clip, `x265-amp.mp4`). The mock gains these knobs:
- `MOCK_WEDGE_AT=n`: from picture n, phase 1 completes and phase 2 never does, as on the Pi.
- Logging of the mailbox clock-off and of any register access while the clock is off.
- `MOCK_WEDGE_RESET` (a clock off/on revives the block).
- `MOCK_PA_HIGH_SIZE` (memory of that size above 16 GiB).
- `MOCK_DEAD_PATH` (the marker's file).

| arm | what must hold | result |
|---|---|---|
| stop (`MOCK_WEDGE_AT=3`) | `timeout in phase 2 … (clock switched off)`, marker `wedged … phase 2 timed out`, `MOCK clock off`, no register access after it, retry at the IRAP refused in-process, every emitted frame right by pts | ok: 92 of 120 frames emitted, all right (wrong=0); 28 dropped up to the IRAP; marker `wedged … phase 2 timed out` |
| next (a new process) | `CPU decode: the block stopped responding earlier in this boot`, no picture and no clock call reach the mock, BIT-EXACT | ok |
| reset (`FFMPEG_RPIVID_RESET=1`) | clock off then on, `works again after the clock reset`, BIT-EXACT on the block, marker removed | ok: BIT-EXACT 120/120, marker removed |
| resetfail (`RESET=1`, block still stuck) | frames right by pts, marker `reset-tried … after a clock reset` | ok: marker `reset-tried … after a clock reset` |
| once (`RESET=1` again) | CPU at once, no picture and no clock call reach the mock | ok |
| oldboot (marker time in the future) | ignored and removed, BIT-EXACT on the block | ok: BIT-EXACT 120/120 |
| reach (`-zc`, the picture buffer above 16 GiB) | `drm_prime output: no contiguous memory the block reaches …: system-memory frames for this stream`, BIT-EXACT on the block, 0 DRM frames | ok: `no contiguous memory the block reaches for 1474560 bytes`, BIT-EXACT |

All earlier arms still pass: default `same=39`, LOOP, LOOP-ISR, LOOP-ZC and LOOP-DROP.

**Pi check (round 4):**
- The build 63 sequence: HLS session, MSE session, then the demo.
  - Expected: `rpivid-stat … stale=0` and `p2 ≈ 3 ms` throughout, no timeout, and MSE `zc_painted` ≈ `painted`.
  - The `rpivid-bo: … PA` lines show where the pictures live.
- If a timeout still happens:
  - The failing process logs `timeout in phase N … (clock switched off)`.
  - Every later player logs `CPU decode: the block stopped responding earlier in this boot` and plays at once on the CPU, with no stall.
  - `cat /tmp/.rpivid.dead` shows the record.
- Optional, after a timeout: one player with `FFMPEG_RPIVID_RESET=1` shows whether a clock off/on revives the block.

## 8. Top risks

1. **`v3d_sand8_blit` on our geometry and clock.** It has not been run on Phoenix. Its correctness on this layout and its GPU time are the largest unknowns. Checkpoint 1 resolves both before any WebKit rebuild.
2. **Per-draw re-blit and shadow memory** (§2.3). They are bounded and measurable. The mitigation is designed (blit once into RGBA), but it costs bandwidth.
3. **Mid-stream CPU fallback in zero-copy mode.** The DPB must be re-materialised as planar frames (one hitch of ~60 ms), and consumers must handle a per-frame format change. It is host-testable with the mock's fault injection.
4. **Uncached SAND buffers.** Every CPU read (fallback, snapshot, check tool) is about 1.5× slower than today's cached path. This is fine as long as those reads stay rare. Do not add a CPU consumer on the hot path.
5. **Process and IPC limits.** The decoder becomes one more render-server client in the WebProcess: a fence-page slot (60 total), BO slots and the `max_bos` budget. With path C, the low-memory budget must also cover the pool. Check `GET_INFO` `max_bos` against browsers with several players.
7. **WebKit side (step 3).**
   - The SAND blit is redone on every composite of a picture (imported BOs are never "private" in Mesa). That is 3.9 ms of GPU at 1080p, and also while a paused video sits under an animating page. If the GPU saturates at 60 fps, use the mitigation in §4.2.
   - Shadow memory: up to one tiled shadow (~3.1 MB) per decoder buffer in the cache.
   - After a fallback, slice threads make the CPU decoder slower than frame threads would (only relevant until the stream ends: the block is not retried).
   - `fstat` IPC per new picture.
   - First use on the Pi of `thread_local` in this code (WTF uses it).
6. **Sequencing with MSE.** Patch 0032 modifies files that 0030 adds and that the MSE work is changing now. Rebase it after MSE lands (W41:153). Its diff should touch only the layer buffer and decoder-open code.
