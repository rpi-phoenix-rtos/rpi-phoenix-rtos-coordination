# vkquake-drm at 10.4 fps — where the frame goes

*2026-09-27. Source cycle: `mig-vkq` (MIGRATION §6.3/§6r),
`artifacts/rpi4b-uart/rpi4b-uart-20260927-130722-mig-vkq.log`. Old-lane reference: the same day's
showcase gate, `artifacts/rpi4b-uart/rpi4b-uart-20260927-123031-b14-gate-vkq.log`. Everything below
is read from those two logs and from source; nothing here has run on the Pi yet except the two
cycles named. Tags: **[measured]** = in a log, **[read]** = in source, **[inferred]** = reasoning.*

## 0. The short version

* **The regression is 2.2×, not 7×.** The "73 fps" of the old lane is vkQuake's on-screen
  `scr_showfps` read off a video capture on 2026-09-08 (`docs/misc/2026-09-08-glamor-screen-mirror-and-gl-window-rate.md` §3,
  quoted in `id1/autoexec.cfg`). The like-for-like counter, the old winsys's `flipstat`, reads
  **22.9 fps median** (48 windows, 17.1–24.6) on the same map on the same day. The new lane's
  `flipstat` reads **10.4**. [measured]
* **The GPU does ~74 ms of work per frame, and the CPU does not overlap it.** Frame 95.9 ms =
  GPU busy 73.7 ms (render 43.6 + compute 28.3 + bin 1.8) + GPU idle 22.2 ms. The present call
  itself blocks 76 ms (median of 45 windows), i.e. about the whole GPU frame. [measured]
* **The render half is attachment traffic, not geometry.** Upstream vkQuake's frame is five
  full-screen 1080p render jobs (the old glue's was one): WBOIT (`r_oit 1`, on by default) turns
  the main pass into three v3dv jobs, the GUI + post-process pass into two, and every one of those
  job boundaries stores and reloads 1080p attachments — ≈ 115 MB of attachment traffic per frame
  against ≈ 17 MB in the old lane, with an A2B10G10R10 colour buffer that V3D 4.2 keeps at **16F
  (8 B/px) in the tile buffer**, halving tile size. [read + inferred; the job count is measured]
* **H1 (uncached BO mappings) is not the cause.** Both lanes map every default BO the same way:
  `MAP_UNCACHED` = Normal Non-cacheable (MAIR attr 0x44), which is exactly what Linux's v3d driver
  gives its BOs (write-combined). A cached + explicitly maintained memory type would change
  nothing that differs between the lanes; not implemented. The same holds for the GL clients.
* **Fix shipped as variants (not yet on hardware):** `r_oit 0` by default on Phoenix and an RGBA8
  colour buffer on V3D (vkQuake patches 0006/0007 in `patches-vkquake-perf/`), plus a wait-time
  line in the hooks to locate the CPU-side block. Pre-registered cycles `perf-vkq-a` / `-b` (§6).

## 1. The two counters

| | old lane (`b14-gate-vkq`) | new lane (`mig-vkq`) |
|---|---|---|
| binary | `/usr/bin/vkquake` (ports/vkquake, fb0 shim `pl_phoenix_vk_vid.c`, old v3dv fork, in-process winsys) | `/usr/bin/vkquake-drm` (same vkQuake commit `1aa13a56`, upstream `gl_vidsdl.c`, SDL KMSDRM Vulkan, Mesa 26.2 v3dv, rpi4-v3d-async + rpi4-kms) |
| scene | `map start`, spawn view, GPU-compute lightmaps | `+map start`, spawn view, GPU-compute lightmaps, `+r_rtshadows 0` |
| V3D clock | 500 MHz (`v3d-coldstate: clk_v3d … meas=500000992`) | 500 MHz (`V3DA srv mmu … clk_meas_hz=500000992`) |
| counter | `v3d-winsys: flipstat` (one per page flip) | `vkquake-drm flipstat` (one per `vkQueuePresentKHR`) |
| steady fps | **22.94 median**, 17.11–24.63, n=48 | **10.38–10.79**, n=45 |
| frame model | record → one `vkQueueSubmit` → `vkDeviceWaitIdle` → pan (synchronous) | FIFO swapchain, 2 images (`kmsbuf id=1,2`), fence-gated flip (`rpi4-kms -G`) |

The old lane is synchronous by design (`pl_phoenix_vk_vid.c:1256`), so its 43.6 ms is CPU **plus**
GPU. The 2026-08-06 characterisation of that lane measured the GPU part at ~30 ms/frame
(`docs/done/2026-09-01-autonomous-plan-archive.md`, F2 row). [measured, older]

## 2. One frame on the new lane

Steady state, `V3DA srv qstat` t=168511 → 378649 ms (210.1 s, 42 windows) against the
`flipstat` totals 98 → 2316 (2218 frames); per-window deltas agree to ±2 % (e.g. t=373645→378649:
886 render jobs / 2287 ms, 328 CSD / 1465 ms, 53 frames). Server in `-m serial` (bin and render
never overlap; `overlap=0ms` throughout). [measured]

| per frame (95.9 ms at 10.43 fps) | jobs | ms | per job |
|---|---|---|---|
| render (CT1) | **16.8** | **43.6** | 2.56 ms |
| compute (CSD) | **6.2** | **28.3** | 4.49 ms |
| bin (CT0) | 16.8 | 1.8 | 0.105 ms |
| TFU | 0 | 0 | — |
| **GPU busy** | | **73.7** (76.9 %) | |
| GPU idle | | **22.2** | |
| binner out-of-memory events | 0.99 | | |
| `vkQueuePresentKHR` (hooks) | | **76.0** median (56.7–78.0) | |

Reference points for the render row [measured]:

* **vkcube** on the same stack (`m5c-vkcube`): **1 render job per frame, 2.78 ms**, at 60 fps.
  A 1080p clear + cube + store costs ~2.8 ms; a render job's fixed cost is not what makes 43 ms.
* **bin jobs average 0.105 ms** including the server's kick prologue (TLB flush, two waited L2T
  flushes) and IRQ-to-completion latency, so the per-job fixed cost of the server is ≤ 0.1 ms.
  The 2.56 ms render and 4.49 ms CSD averages are the GPU executing, not the server's overhead
  (the CSD completion is IRQ-driven: `INT_CSDDONE` is in `CORE_IRQS`, `v3da_regs.h:135`).

### Timeline (inferred from the counters; not traced)

```
t=0        GPU starts frame N (its submit waited on the acquire of the image frame N-2 used)
t≈0..28    CSD: lightmap update, warp textures, indirect-draw culling      (~6 dispatches)
t≈28..72   render: warp mips + staging copies (~12 small jobs), then the five 1080p jobs
t≈74       GPU idle; frame N's fence passes; rpi4-kms applies the flip at the next vblank
t≈74..96   vkQueuePresentKHR(N) returns; only then can frame N+1 be submitted
           (~20 ms later); GPU idle all this time
t≈96       GPU starts frame N+1
```

The GPU is idle 22 ms per frame because frame N+1 cannot be *submitted* until `vkQueuePresentKHR(N)`
returns, and that call blocks until about the end of frame N's GPU work. (vkQuake runs the end of the
frame — acquire, submit, present — as a task with `r_tasks 1`, so the main thread may already be
recording N+1 during the block; what the counters support is the submit ordering, not a CPU partition.) **Where** in the present
path it blocks is not settled by reading (candidates, all read: Mesa WSI's throttle
`WaitForFences` on the image's previous present fence, `wsi_common.c:2462`; the semaphore
payload copy `vk_drm_copy_sync_file_payloads` / `spin_wait_for_sync_file`,
`vk_drm_syncobj.c:288-295`; the NONBLOCK atomic commit through libdrm-phoenix's G13
`implicit_attach`, `drm_phoenix_kms.c:240`). §5 adds the measurement that decides it.

## 3. Why the render row is 43.6 ms: five 1080p jobs, heavy attachments

[read] Upstream `GL_EndRenderingTask` (`gl_vidsdl.c:3999-4253`) records per frame:

1. the **main render pass**, `MAIN_RENDER_PASS_OIT` because `r_oit` defaults to 1
   (`gl_rmain.c:71`; `GL_FrameOITModeForCvarValue` → `OIT_MODE_WBOIT`, `gl_vidsdl.c:3493-3510`):
   subpass 0 = scene colour + depth, subpass 1 = WBOIT accum `R16G16B16A16_SFLOAT` + reveal `R8`
   + depth, subpass 2 = scene colour + depth reading accum/reveal as **input attachments**
   (`gl_vidsdl.c:1739-1762`);
2. the **UI render pass**: subpass 0 = GUI into the scene colour buffer (`loadOp LOAD`), subpass 1 =
   post-process into the swapchain image reading the scene colour as an input attachment.

v3dv merges consecutive subpasses into one job only when they use the same colour and depth
attachments (`cmd_buffer_can_merge_subpass`, `v3dv_cmd_buffer.c:323-390`). None of these pairs
does, so the frame is **3 + 2 = 5 render jobs at 1920×1080**, and every boundary stores the
attachments the next job loads. The old glue's frame was **one** job: world + 2D in a single
subpass straight into the RGBA8 scanout image with a D32 depth (`pl_phoenix_vk_vid.c:932-937`,
`1142-1150`; `frame_oit_mode` never leaves `OIT_MODE_NONE` there because the glue replaces
`GL_BeginRendering`).

The colour format matters twice. vkQuake picks `A2B10G10R10_UNORM_PACK32` when the device allows
it (`gl_vidsdl.c:1430-1440`; the log prints `Using A2B10G10R10 color buffer format`), and V3D 4.2
keeps RGB10_A2 in the tile buffer at **16F** (`v3dvx_formats.c:368-374`), i.e. 64 bpp. Tile size on
V3D 4.x drops one step per internal-bpp step and per RT-count step (`v3d_choose_tile_size`,
`v3d_util.c:182-199`; v3dv passes the *subpass's* colour count and bpp, `v3dv_cmd_buffer.c:1800-1808`):

| job (1080p) | RTs, max bpp | tile | tiles | attachment traffic (MB) [inferred] |
|---|---|---|---|---|
| old lane: the only job | 1, 32 | 64×64 | 510 | store RGBA8 8.3 (+ depth store ≤ 8.3) |
| main sp0 (opaque) | 1, 64 (RGB10A2→16F) | 64×32 | 1020 | store colour 8.3 + store depth 8.3 (the next job loads it) |
| main sp1 (translucent) | 2, 64 (accum 16F) | 32×32 | 2040 | load depth 8.3 + store accum 16.6 + reveal 2.1 |
| main sp2 (OIT resolve) | 1, 64 | 64×32 | 1020 | load colour 8.3 (+ depth 8.3) + sample accum/reveal 18.7 + store colour 8.3 |
| UI sp0 (GUI) | 1, 64 | 64×32 | 1020 | load colour 8.3 + store colour 8.3 |
| UI sp1 (post-process) | 1, 32 (swapchain) | 64×64 | 510 | sample colour 8.3 + store swapchain 8.3 |
| **new lane total** | | | **5610** | **≈ 110–120** |

At the ~3 GB/s one 1080p store costs in vkcube (8.3 MB in 2.78 ms, clear and fixed cost included),
≈ 115 MB is ≈ 35–40 ms — the bulk of the 43.6 ms render row. The remaining ~12 small render jobs per
frame are the warp-texture mip chains (`R_UpdateWarpTextures`, `gl_warp.c:250-284`: 4 `vkCmdBlitImage`
per visible warp texture, each a v3dv blit-shader render job at 256² … 32²) and staging copies
(v3dv copies buffers through the TLB, i.e. as render jobs); at ≤ 0.3 ms each they are ≤ 4 ms.
[inferred]

**Prediction for `r_oit 0`:** the main pass becomes one job (sp0 only, depth `DONT_CARE`), traffic
falls to ≈ 42 MB (the depth store is `STORE` only with OIT, `gl_vidsdl.c:1635`), render ≈ 15–25 ms/frame, render jobs −2 per frame. **RGBA8 on top:** the colour
buffer's tiles double in size (64×64) for the main and GUI jobs; the bytes stay the same (both
formats are 4 B/px in memory), so the extra gain is the per-tile cost only: 1–5 ms. [inferred]

`r_oit 0` is upstream's classic path (sorted alpha, `r_alphasort 1`, `gl_rmisc.c:108`); the old
lane ran without OIT for its whole life. RGBA8 is what upstream uses when 10-bit is unsupported and
what the old lane used.

## 4. The compute row (28.3 ms) — measured, not yet attributed

6.2 CSD jobs/frame at 4.49 ms each. The per-frame dispatches are [read]: the lightmap update
(`R_FlushUpdateLightmaps`, one `vkCmdDispatch` per dirty lightmap region, `r_brush.c:3407-3496`),
the indirect-draw culling (clear + draw, `r_brush.c:3512-3547`) and one `cs_tex_warp` dispatch of
64×64 workgroups per visible warp texture (`r_waterwarpcompute 1`, `gl_warp.c:120-131`). The old lane
ran the same engine code; its 2026-08-06 A/B put the lightmap compute at ~3 ms/frame. The new
number is ~9× that, and neither the attachment argument nor the server's per-kick cost (≤ 0.1 ms,
§2) explains it. `r_lerplightstyles 1` makes lightstyles change every frame, so the lightmap work
does not shrink as fps rises. Diagnostic cycles `perf-vkq-c` / `-d` (§6) split it by dispatch
source; neither variant is a fix (`r_gpulightmapupdate 0` moves lighting to the CPU path).

## 5. H1–H4 verdicts

**H1 — uncached host-visible memory: not the regression.** [read]
* New lane: DRM `CREATE_BO` carries no flags (`drm_phoenix_v3d.c:564-565`), so the server allocates
  `MAP_CONTIGUOUS | MAP_UNCACHED` (`v3da_bo.c:105-109`) and the client maps the memref the same way
  (`xf86drm_phoenix.c:1030`). Old lane: default BOs are `MAP_CONTIGUOUS | MAP_UNCACHED` too
  (`v3d_phoenix_winsys.c:1752-1758`).
* `MAP_UNCACHED` → `PGHD_NOT_CACHED` → `MAIR_IDX_NONCACHED` (`pmap.c:505-512`), slot 1 of
  `MAIR_EL1 = 0x444FF` = 0x44 = **Normal Non-cacheable**, not Device-nGnRE. That is what Linux's v3d
  gives its shmem BOs (write-combine = `MT_NORMAL_NC` on arm64), and what Mesa v3dv's single memory
  type (`DEVICE_LOCAL | HOST_VISIBLE | HOST_COHERENT`, `v3dv_device.c:1534-1537`) is written for.
* Size of the effect [inferred]: vkQuake streams into its dynamic VB/IB/UB (256 KB / 1 MB / 256 KB
  rings; a start-map frame uses a small fraction) and v3dv writes the CLs and uniform streams of ~40
  jobs — order 10² KB/frame of **stores**, which Normal-NC gathers; the hot paths do not read mapped
  memory. Even at a pessimistic 0.5 GB/s that is < 1 ms of a 96 ms frame.
* So a cached + clean/invalidate memory type (or a `HOST_CACHED` non-coherent type) would be real
  work for no measurable gain here, and would add a coherency obligation (V3D is not IO-coherent
  with the ARM caches) to every CPU write path. **Not implemented.**
* **GL clients (quake2-drm, stk-drm, quakespasm-drm):** Mesa gallium v3d on this lane maps its BOs
  through the same `CREATE_BO` → uncached path, and so did the old in-process winsys. No gain to
  expect from a caching change there either; their costs are elsewhere (E2b for STK, frame-pacing
  for the 30 fps lock).

**H2 — per-submit synchronous waits: partly.** There is no per-*submit* round trip that matters
(bin jobs incl. server overhead average 0.1 ms), but the *frame* is serialised: GPU idle 22 ms/frame
while the CPU records, because `vkQueuePresentKHR` blocks ≈ the GPU frame (§2). `-m serial` costs at
most the bin time (1.8 ms/frame, `overlap=0`); `-m pipeline` is not where the time is.

**H3 — `r_gpulightmapupdate` compute: real, 28.3 ms/frame, unattributed (§4).** GPU busy is 77 % of
wall time: the lane is mostly GPU-bound, with the remaining 23 % lost to serialisation.

**H4 — slow paths from patches / shader compiles: no.** Patch 0005 only skips the timestamp query
pool (two `vkCmdWriteTimestamp` per frame). Pipelines are built once at start (`Creating pipelines`,
73 s to the first present); no per-frame compile shows in the log.

## 6. Variants and pre-registered cycles

### 6.1 What was built (2026-09-27, host only)

| | `vkquake-drm-perf-a` | `vkquake-drm-perf` |
|---|---|---|
| out dir | `tools/gpu-lane/sdl2-drm/build-out/vkquake-drm-perf-a/` | `…/build-out/vkquake-drm-perf/` |
| vkQuake patches | 0001–0005 + **0006** (`r_oit` 0) | 0001–0005 + **0006** + **0007** (RGBA8 colour buffer on vendor 0x14E4) |
| `vkquake-drm.stripped` | `a9beda48b6719fe6…` (13 368 776 B) | `5fbf78995f11e089…` (13 368 776 B) |
| unstripped (addr2line) | `d4287d904ba84e5e…` | `725849e141fac1ac…` |
| launcher | `vkq-drm` `73e0d6a709b2f851…` → execs `/usr/bin/vkquake-drm-perf-a` | `vkq-drm` `ee0e3079adb23761…` → execs `/usr/bin/vkquake-drm-perf` |
| staged | `/usr/bin/vkquake-drm-perf-a`, `/bin/vkq-drm-perf-a` | `/usr/bin/vkquake-drm-perf`, `/bin/vkq-drm-perf` |

Both: same ICD (`69c689ad…`, Mesa patch set `60dd139d…`), same libdrm-phoenix m5b
(`a508e207…`), same SDL; 83 TUs, 0 warnings, the script's symbol/string/call-site proofs pass,
12 guarded shared files unchanged. **Confound vs the baseline:** both variants link the sysroot's
current `libphoenix.a` `2acb195e…` (queue40 / build 15 had moved it), while the `mig-vkq` binary
linked `e69b216a…`; a and b share the same libphoenix, so a-vs-b is one variable, but baseline-vs-a
is two (a libc delta is not expected to move GPU-side qstat rows; CPU-side rows may). The perf cycles
will also boot a newer kernel image than `mig-vkq` (build 14) — record the `loader.disk` sha per cycle.
Staged with `sudo -n install -m 755` on
`/srv/phoenix-rpi4-nfs-gcc16` and `cmp`-checked; `/usr/bin/vkquake-drm` (`20e3d43f…`) and
`/bin/vkq-drm` (`aaf70271…`) — the queued `mig-vkq` binaries — are untouched.

Changes behind them (all in `tools/gpu-lane/sdl2-drm/`):

* `patches-vkquake-perf/0006-…`, `0007-…` — kept out of `patches-vkquake/` so a default
  rebuild of `vkquake-drm` does not pick them up before hardware says so. **Promote both into
  `patches-vkquake/` if `perf-vkq-b` passes** (rename to the next free numbers, rebuild the default).
  *Done after `perf-vkq-b`: they are `patches-vkquake/0006`, `0007` (same numbers, the next free
  ones), `patches-vkquake-perf/` is gone, see "Adopted" at the end.*
* `build-vkquake-drm.sh`: env `VKQDRM_EXTRA_PATCHES` (patch files applied after
  `patches-vkquake/`, part of the source stamp and `BUILD-INFO.txt`) and `VKQDRM_TARGET` (the
  launcher's exec path, `-DVKQDRM_TARGET`). Defaults unchanged.
* `vkqdrm/vkq-drm-launcher.c`: exec path from `VKQDRM_TARGET` (default `/usr/bin/vkquake-drm`);
  the `vkq-drm: exec …` line now also lists appended arguments (identical text without them).
* `vkqdrm/vkqdrm_hooks.c`: new **`vkquake-drm waitstat`** line after each `presentstat`:
  calls / average µs / max µs of `vkAcquireNextImageKHR`, `vkQueueSubmit`, `vkWaitForFences`
  and `vkWaitForPresent2KHR` in the window (wrapped at the same two lookups as the present).

Build commands (historical: the `patches-vkquake-perf/` paths no longer exist; the default build
now applies both):

```
VKQDRM_OUT=tools/gpu-lane/sdl2-drm/build-out/vkquake-drm-perf-a VKQDRM_TARGET=/usr/bin/vkquake-drm-perf-a \
  VKQDRM_EXTRA_PATCHES="tools/gpu-lane/sdl2-drm/patches-vkquake-perf/0006-gl_rmain-r_oit-off-by-default-on-phoenix.patch" \
  tools/gpu-lane/sdl2-drm/build-vkquake-drm.sh
VKQDRM_OUT=tools/gpu-lane/sdl2-drm/build-out/vkquake-drm-perf VKQDRM_TARGET=/usr/bin/vkquake-drm-perf \
  VKQDRM_EXTRA_PATCHES="tools/gpu-lane/sdl2-drm/patches-vkquake-perf/0006-gl_rmain-r_oit-off-by-default-on-phoenix.patch tools/gpu-lane/sdl2-drm/patches-vkquake-perf/0007-gl_vidsdl-rgba8-color-buffer-on-v3d.patch" \
  tools/gpu-lane/sdl2-drm/build-vkquake-drm.sh
```

**Why a compile-time default, not `+r_oit 0` on the command line:** the render resources (and the
OIT attachments) are created at the first `GL_BeginRendering`, during `Host_Init`, before
`quake.rc` runs the `+` commands. A later `r_oit 0` changes `frame_oit_mode` and takes the
`VID_Restart` path — swapchain teardown and re-creation, which has never run on this lane and
would confound the measurement. The `config.cfg`/`autoexec.cfg` on the export set no `r_oit`
(checked), so the default holds.

### 6.2 Cycles (one launch each; MIGRATION §6.3 form; one Pi cycle at a time)

No server, libdrm-phoenix, ICD or KMS change is involved — only the vkQuake binary and its hooks
— so **vkcube needs no re-check**; `m5c-vkcube`'s command stays the optional control.

**`perf-vkq-a`** — does WBOIT cost what §3 says?

```
./scripts/test-cycle-psh-interact.sh --label perf-vkq-a --wait-secs 220 --inter-cmd-secs 8 --idle-secs 60 \
    --max-cmd-secs 300 --ready-line 'V3DA srv detached|KMS srv detached' --ready-extra-secs 20 \
    --hdmi-dense-on 'vkquake-drm: new GPU lane' -- \
    "/bin/rpi4-v3d-async-m3p2 -r 1 -m serial -i" \
    "/bin/rpi4-kms-gate -G" \
    "/bin/vkq-drm-perf-a"
```

**`perf-vkq-b`** — the candidate fix (OIT off + RGBA8):

```
./scripts/test-cycle-psh-interact.sh --label perf-vkq-b --wait-secs 220 --inter-cmd-secs 8 --idle-secs 60 \
    --max-cmd-secs 300 --ready-line 'V3DA srv detached|KMS srv detached' --ready-extra-secs 20 \
    --hdmi-dense-on 'vkquake-drm: new GPU lane' -- \
    "/bin/rpi4-v3d-async-m3p2 -r 1 -m serial -i" \
    "/bin/rpi4-kms-gate -G" \
    "/bin/vkq-drm-perf"
./scripts/check-torch-rois.py --label perf-vkq-b
```

**`perf-vkq-c` / `perf-vkq-d`** (diagnostic, only if `perf-vkq-b` leaves CSD ≥ 15 ms/frame): the
same command with `"/bin/vkq-drm-perf +r_gpulightmapupdate 0"` (c: lightmap compute → CPU path)
and `"/bin/vkq-drm-perf +r_waterwarpcompute 0"` (d: warp textures → raster render passes). Both
cvars switch at run time without a `VID_Restart`. Neither is a fix.

Frame quantities are computed as in §2: qstat deltas over the steady windows (skip the first two
after `player entered the game`) divided by the `flipstat` frames over the same span.

| Line / quantity | baseline `mig-vkq` | predicted `perf-vkq-a` | predicted `perf-vkq-b` | if instead… |
|---|---|---|---|---|
| `vkq-drm: exec /usr/bin/vkquake-drm-perf[-a] …`, banner | — | once | once | stale launcher / wrong staging (`cmp`) |
| colour-format line | `Using A2B10G10R10 …` | same | `Using R8G8B8A8 color buffer format (V3D: …)`, no A2B10G10R10 line | 0007 not in the binary |
| render jobs / frame | 16.8 | **14.8 ± 1** (−2: OIT sp1, sp2) | 14.8 ± 1 | ≈ 16.8: OIT still on (a config sets `r_oit`) |
| render ms / frame | 43.6 | **15–25** | **12–22** (a − 1…5) | a > 35: the attachment-traffic model is wrong → per-job duration histogram in the server next |
| CSD jobs, ms / frame | 6.2, 28.3 | 6.2 ± 1, 28 ± 5 | same | CSD moves with OIT: it is not independent of the render passes (a finding) |
| GPU busy ms / frame | 73.7 | 45–55 | 42–52 | — |
| `flipstat` fps | 10.4 | **12–17** | **13–19** | ≥ 20: the idle gap shrank too (serialisation scales with GPU time); < 11: something else regressed — diff the qstat rows |
| `presentstat present_us_avg` vs GPU ms/frame | 76 ms vs 74 | **within ±20 %** of the GPU frame | same | present ≪ GPU frame: see waitstat |
| `waitstat acquire` avg | — | **< 2 ms** | < 2 ms | acquire ≈ GPU frame and present small: the block is FIFO/flip pacing in acquire → frame-pacing agent's area, not this lane's present path |
| `waitstat fence` / `pwait` avg | — | < 5 ms each | same | fence ≈ GPU frame: vkQuake's own `command_buffer_fences` wait (2-deep) is the serialiser |
| HDMI | lit, torches | lit, torches; translucent particles/sprites drawn (sorted alpha) | same, no visible banding beyond the old lane's (also RGBA8) | missing translucents: the non-OIT path is broken on v3dv |
| torch ROI check | inconclusive (viewpoint) | — | PASS or inconclusive for the same reason | torches dark at the viewpoint: a real finding |
| `V3DA srv qstat` | err=0 wedges=0 | err=0 wedges=0 rej=0 | same | any: FAIL |
| faults | 0 | 0 | 0 | addr2line the unstripped binary of that out dir |

**What the cycles decide:** (a) confirms or refutes the WBOIT cost on its own; (b) is the
candidate for promotion — if it renders correctly and reaches ≥ 13 fps, 0006/0007 move into
`patches-vkquake/`. The waitstat columns decide where the ~22 ms/frame GPU idle comes from
(present path vs acquire vs the engine's fence), which is the next lever once the GPU frame is
smaller; the 23 fps old-lane parity additionally needs the CSD row explained (c/d).

## Result — `perf-vkq-a` / `perf-vkq-b` (queue43, 2026-09-27 15:00–15:15)

| cycle | variant | fps median (n = 45 windows) | V3DA over the run (360 s window) | waits (avg) |
|---|---|---|---|---|
| mig-vkq (baseline) | upstream defaults | 10.4 | render 43.6 ms/frame, csd 28.3 ms/frame | present ≈ 76 ms |
| **perf-vkq-a** | 0006 (`r_oit 0`) | **15.46** | render 52224 jobs / 67.2 s, **csd 20140 / 80.0 s**, busy 151 s, `oom=3474` | acquire 0.5–0.7 ms, submit 0.08 ms, fence 0.06 ms |
| **perf-vkq-b** | 0006 + 0007 (RGBA8) | **17.06** | render 57849 / 64.7 s, csd 21077 / 75.6 s, busy 143 s, `oom=3849` | as a |

Logs `artifacts/rpi4b-uart/*-perf-vkq-{a,b}.log`; HDMI `artifacts/hdmi/20260927-151424-perf-vkq-b-tick.png`: the same view
as mig-vkq, lit, torches, "19 FPS" on screen; 0 exceptions both. ROI torch check INCONCLUSIVE again (viewpoint mae > 8
on a frame that matches mig-vkq by eye, so the reference viewpoint, not the render, differs).

**Reading:** both predictions held (a: 12–17 → 15.5; b: 13–19 → 17.1). The waits are now small, so the frame is
GPU-bound, and **compute is now the largest GPU row** (≈ 75–80 s of 360 s against render's 65 s). Next: attribute the
compute (`perf-vkq-c` `+r_gpulightmapupdate 0`, `-d` `+r_waterwarpcompute 0`), and read `oom` (binner overflow
allocations: 3474/3849 over the run, not free). The old lane's like-for-like 22.9 fps is still 1.34× ahead.
Adopt 0006+0007 into `patches-vkquake/` (gate passed: b ≥ a, same picture).

## Adopted (2026-09-27, after build 17)

`patches-vkquake-perf/0006-…` and `0007-…` are now **`tools/gpu-lane/sdl2-drm/patches-vkquake/0006-gl_rmain-r_oit-off-by-default-on-phoenix.patch`**
and **`0007-gl_vidsdl-rgba8-color-buffer-on-v3d.patch`** (unchanged; 0006/0007 were already the next free
numbers); `patches-vkquake-perf/` is gone, `VKQDRM_EXTRA_PATCHES` stays as the generic variant hook. The
default `build-vkquake-drm.sh` build (patch set `46e27a38…`, 0001–0007; SDL from the default tree, which now
also carries the SDL swap reorder `patches/0009` — irrelevant for vkQuake, which presents through the Vulkan
WSI): `vkquake-drm.stripped` 13 368 776 B **`22755bb450b09e0f…`**, unstripped `c656f27c61e0230d…`, launcher
`vkq-drm` **`e49a7444fc782d0f…`** (execs `/usr/bin/vkquake-drm`); Vulkan-SDL `libSDL2.a` `1de303a9…`; ICD
`69c689ad…` and libdrm-phoenix m5b unchanged, libphoenix.a `2acb195e…` (as the perf variants). Checks: the
script's proofs pass; gdb on the unstripped ELF `r_oit.string = "0"`; `Using R8G8B8A8 color buffer format
(V3D: …)` present. Combined Pi check: MIGRATION §6.5 `mig-all-vkq` (predicted ≈ 17 fps).
