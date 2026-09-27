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
number is ~9× that *(not like-for-like: see "CSD per-job cost" at the end — the matching new-lane number is 5.2 ms/frame)*, and neither the attachment argument nor the server's per-kick cost (≤ 0.1 ms,
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

## Result — `perf-vkq-c` / `perf-vkq-d` (queue47, 2026-09-27 16:35–16:55): the compute row, attributed

Both on the perf-b binary (0006+0007), one cvar each; 45 windows each, 0 exceptions.

| cycle | cvar | fps median | CSD jobs / ms (360 s window) | render jobs / ms |
|---|---|---|---|---|
| perf-vkq-b | — | 17.06 | 21077 / 75623 | 57849 / 64657 |
| **perf-vkq-c** | `+r_gpulightmapupdate 0` (lightmaps on the CPU) | **19.78** | 13186 / 63067 | 67303 / 74407 |
| **perf-vkq-d** | `+r_waterwarpcompute 0` (warp via raster passes) | **18.13** | 13253 / 55161 | 73319 / 76352 |

**Reading:** each compute user carries about 8 000 of the 21 000 CSD jobs. Removing either one moves time from compute to
render (more frames per second means more render jobs) and gains 1–2.7 fps. The rest (~13 000 jobs, 55–63 s) is
common to both *(corrected in "CSD per-job cost" §2: that is the count remaining after one removal; the common part is ≈ 0.8 jobs/frame, and `-c` also removes the indirect-draw compute)*, so it is neither the lightmap update nor the water warp: most likely the per-frame compute of
vkQuake's other GPU paths (particles / indirect draw setup). The per-job cost is still the question: 4.5 ms
average per CSD job against about 3 ms/frame of lightmap compute on the old lane. Next: time one CSD job's dispatch
size and its QPU/TMU cost in the render server (a `csd` detail line), and compare with the old lane's compute
dispatch for the same shader. Shipping `+r_gpulightmapupdate 0` by default is a cheap +16 % if the CPU path looks
identical on HDMI (to check).

## CSD per-job cost (2026-09-27 evening, host only)

*Question: why does a V3D compute (CSD) job cost ~4.5 ms on the new lane when the old lane put the lightmap
compute at ~3 ms/frame? Sources: the qstat/flipstat lines of `mig-vkq`, `perf-vkq-{a,b,c,d}`, `mig-all-vkq`
(steady state = the last 40 qstat windows, 200 s, frames from the flipstat totals over the same span); the old
lane's `b18-gate-vkq` (flipstat only: the old winsys prints no CSD timing unless built with `V3D_SP`, and no
archived vkQuake log has a `subprof-x` line); source as cited. Tags as above.*

### Short version

* **There is no per-job CSD cost that the new lane adds.** The CSD path of `rpi4-v3d-async` is the old
  winsys's `ioc_submit_csd` step for step, the CFG words are Mesa's, unchanged, and Mesa is the same release
  on both lanes. Every suspect on the list (batches, wg_size, supergroups, QPU count, per-job L2/TLB
  maintenance, serial mode, IRQ + wake latency) is either identical in the two lanes or bounded by the bin
  jobs' 0.105 ms all-in cost (below). [read + measured]
* **The "~9×" was not a like-for-like comparison.** It set the new lane's *whole* compute row (28.3 ms/frame,
  every compute user, OIT still on) against the old lane's *lightmap-only* A/B delta (~3 ms/frame, a frame-time
  difference on a synchronous lane, so GPU saved minus the CPU `R_BuildLightMap` added: the old GPU number is
  ≥ 3 ms). The like-for-like pair is `perf-vkq-b` → `-c`: `r_gpulightmapupdate 0` removes **5.2 ms/frame** of
  CSD on the new lane. So ~1.5–1.7×, not 9×, and within what a different vkQuake frame (five passes, not one)
  can explain. [measured]
* **Two errors in the `-c`/`-d` reading above**, corrected here: `r_gpulightmapupdate 0` also turns off the
  **indirect-draw compute** (`indirect = r_indirect && indirect_ready && r_gpulightmapupdate && !scr_speeds`,
  `gl_rmain.c:1536`), so `-c` removed lightmap **+ indirect**; and "~13 000 jobs common to both" is the
  count *remaining* after one removal, not the common part. [read]
* **The redone decomposition leaves a third compute class that neither suspect explains** — ≈ 0.8 jobs/frame
  at ≈ 10 ms each, ~40 % of the compute row — and reading vkQuake's five dispatch sites does not name it. The
  render server now carries the instrument that does (`-C`, a per-pipeline CSD profile); cycle `perf-vkq-e`
  (below) is its first run. No fix is claimed: the fix follows the attribution.
* **Side finding:** at `perf-vkq-b` the GPU is busy **36.9 of 57.9 ms** per frame (64 %). With acquire /
  submit / fence waits at 0.7 / 0.08 / 0.06 ms the remaining ~21 ms/frame is neither GPU work nor a hooked
  wait, so the "now GPU-bound" reading of the a/b result is too strong. That idle time is a lever of the same
  size as the whole compute row. [measured]

### 1. The kick and completion paths, side by side [read]

| step | old lane (in-process, `v3d_phoenix_winsys.c:3744-3842` `ioc_submit_csd`) | new lane (`v3da_jobs.c` `kick_csd` :473, CSDDONE completion in `v3da_jobs_events`) |
|---|---|---|
| CPU stores → DRAM | `dsb sy` | `dsb sy` |
| slice caches | `SLCACTL = INVAL_ALL` | same |
| MMU | `mmu_flush_tlb` every job (MMUC flush + TLB clear, both waited) | `tlb_step` = the same, every job (knob `V3DA_KNOB_TLB_ON_CHANGE` off by default) |
| L2T | wait-idle, `L2TFLS`, wait | `l2t_flush(1)`: the same |
| busy unit | spin while `CSD_STATUS.HAVE_CURRENT` (8 M spins) | same (then wedge + reset instead of `reset_reinit_core`) |
| kick | clear CSDDONE, CFG1..6, CFG0 | drain status (CSDDONE own bit dropped), CFG1..6, CFG0 |
| CFG words | `s->cfg[]` from v3dv, unchanged | `d.cfg[]` = `s->cfg[]` from v3dv, unchanged (`drm_phoenix_v3d.c` `ioc_submit_csd`: `memcpy`) |
| completion | spin on `CTL_INT_STS & CSDDONE` in the caller | IRQ (`INT_CSDDONE` in `CORE_IRQS`) → handler → cond → event thread (priority 2) |
| after | wait-idle, `TMUWCF` + spin, `L2TFLS\|FLM_CLEAN`, wait, `dsb` | `clean_caches()` + `dsb`: the same sequence |
| identity v3dv sizes from | hard-coded `IDENT1 = 0x81001422` | live registers: `core0=0x04443356/0x81001422/0x40078121` (log) — the same values |
| hardware set-up | `apply_core_regs` | `apply_core_regs`, "verbatim" (`v3da_hw.c:251`): `MISCCFG` QRMAXCNT + OVRTMUOUT, `L2CACTL`, `AXICFG`, MMU |

* **The CFG words (workgroup counts, wg_size, wgs_per_sg, batches, threading, shader address) are built by
  the client**, in Mesa's `cmd_buffer_create_csd_job` (`v3dv_cmd_buffer.c:4295-4364`), which neither lane
  patches: M5 §3.1's triage found none of the old fork's 9 commits over `mesa-26.2.0` in the compute path.
  The same vkQuake commit (`1aa13a56`) issues the same dispatches. So the GPU runs the same job on both lanes.
* **Server overhead is bounded by the bin row.** A bin job passes through the same prologue class (TLB flush,
  two waited L2T flushes), the same IRQ → event-thread completion and the same bookkeeping; its whole
  kick-to-completion time averages **0.105 ms** (`perf-vkq-b`: 3107 ms / 57 849). A CSD job's extra epilogue
  is one L2T clean. Nothing here can add milliseconds per job; the 3.6–4.5 ms averages are GPU execution.
* **Serial mode costs CSD nothing it did not cost on the old lane**, which was fully synchronous. Pipeline
  mode would not overlap vkQuake's compute with its render passes either: every compute stage is fenced by a
  pipeline barrier (`r_brush.c:3415/3494/3524/3553`, `gl_warp.c:204/242`), and v3dv turns those into job
  serialisation. [inferred]
* **Linux differs in one place:** its CSD job does not flush the MMU TLB (only `v3d_invalidate_caches`; the
  TLB is flushed when page tables change). Both Phoenix lanes flush it per job. A TLB miss costs a page-table
  read; the effect per job is a few hundred µs at most [inferred] and is common to both lanes, so it cannot be
  the lane difference. The server already has the A/B for it (`-k 0x01`, `V3DA_KNOB_TLB_ON_CHANGE`); not
  proposed before the profile shows `pro_us`/`gpu_us` sensitive to it.

### 2. The compute row, decomposed again [measured]

Steady state, per frame (last 40 qstat windows; frames from flipstat):

| cycle | fps | CSD jobs/frame | CSD ms/frame | ms/job | render jobs, ms /frame | GPU busy / frame |
|---|---|---|---|---|---|---|
| mig-vkq (OIT on) | 10.55 | 6.23 | 27.97 | 4.49 | 16.8, 43.1 | 72.9 / 94.8 ms |
| perf-vkq-a | 15.58 | 5.76 | 22.93 | 3.98 | 14.9, 19.2 | 43.2 / 64.2 |
| **perf-vkq-b** | 17.27 | **5.44** | **19.46** | 3.58 | 14.9, 16.7 | **36.9 / 57.9** |
| perf-vkq-c (`+r_gpulightmapupdate 0`) | 19.87 | 2.99 | 14.29 | 4.78 | 15.2, 16.8 | 31.9 / 50.3 |
| perf-vkq-d (`+r_waterwarpcompute 0`) | 18.29 | 3.25 | 13.59 | 4.18 | 17.9, 18.6 | 33.1 / 54.7 |
| mig-all-vkq (b's patches, default build) | 17.22 | 5.46 | 19.64 | 3.60 | 14.9, 16.7 | 37.1 / 58.1 |

(Computed with a scratch parser over the logs' `V3DA srv qstat` and `vkquake-drm flipstat` lines; the
per-window deltas agree with §2's method.)

vkQuake's compute dispatch sites [read]: the lightmap update (`r_brush.c:3490`, w×h workgroups per dirty
region), the indirect-draw clear and draw (`:3517`, `:3541`, one each per frame when `indirect`), the water
warp (`gl_warp.c:130`, 64×64 workgroups per visible warp texture), the screen effects (`gl_vidsdl.c:3807`,
240×135 workgroups — only when under water, `r_scale ≥ 2`, `vid_palettize`, a `v_blend` flash with
`gl_polyblend`, or the menu: none at the spawn view, `gl_vidsdl.c:4070-4071`), and the ray-tracing BLAS
skinning (`gl_mesh.c:1034`, off with `r_rtshadows 0`). v3dv adds compute only for events and query
availability (`v3dv_event.c:510/540`, `v3dv_query.c:804`); vkQuake records neither (timestamps are off, patch
0005). With L+I = lightmap + indirect (both gone in `-c`), W = warp (gone in `-d`), X = anything else:

| class | jobs/frame | ms/frame | ms/job |
|---|---|---|---|
| L+I = b − c | 2.45 | 5.17 | 2.1 (average over I's ~2 small jobs and L) |
| W = b − d | 2.19 | 5.87 | 2.7 |
| **X = c + d − b** | **0.80** | **8.42** | **≈ 10.5** |

The same fit per second instead of per frame (lightmap updates follow the 10 Hz lightstyle clock, the rest
follows frames) gives W = 2.17/frame, lightmap ≈ 7.8 jobs/s, X ≈ 0.83/frame — X survives either model. What
the numbers support: the warp jobs cost ~2.7 ms each (a 512×512 image, 16 384 batches: plausible GPU time at
500 MHz, [inferred]); the lightmap + indirect pair costs about what the old lane's A/B saw, somewhat more; and
the biggest single compute item is something the doc had not named. Candidates by reading, none confirmed:
the screen-effects pass firing after all (a `v_blend` alpha at spawn, `key_dest`), a lightmap dispatch class
whose count is not additive across the two runs, or a v3dv-internal job. The profile answers which.

### 3. The instrument: `rpi4-v3d-async -C` (CSD profile)

* **What:** every completed CSD job is added to a class keyed by its **CFG5** (shader code address |
  THREADING | SINGLE_SEG | PROPAGATE_NANS = one compute pipeline). Per class: count, last CFG0-2 workgroup
  counts, workgroups per job min..max and sum, CFG3 decode (`wgsz`, `sgwgs` = workgroups per supergroup,
  `sgbat` = batches per supergroup), `thr4` (4-thread shader), `seg1`, batches, and four times in generic-timer
  ticks: **`pro_us`** kick prologue (caches, TLB, CFG writes), **`gpu_us`** CFG0 write → first sight of
  `INT_CSDDONE` (stamped in the IRQ handler, the poll path or a pre-kick drain, `csd_done_stamp`), **`wake_us`**
  → the event thread takes it, **`epi_us`** the completion's TMU/L2T clean; `max_us`, `noirq` (no stamp), and a
  gpu-time histogram `h=` over <250/<500/<1000/<2000/<4000/<8000/<16000/≥16000 µs.
* **Output:** one cumulative line per class that ran, printed with each qstat line
  (`V3DA srv csd cfg5=… n=… wg=AxBxC wgs=sum/min..max wgsz= sgwgs= sgbat= thr4= seg1= batches= gpu_us= max_us=
  pro_us= wake_us= epi_us= noirq= h=…`), plus `V3DA srv csdprof on cntfrq=…` once at start. Never per job (the
  archive's 21.4 ms "empty dispatch" was a per-dispatch printf at UART speed). Off without `-C`; the stamp in
  the handler is an `isb; mrs cntvct_el0`, a load and a store, and the handler still makes no call (`objdump`: 126
  instructions, no `bl`/`blr`).
* **Code:** `tools/gpu-lane/v3d-async/v3da_csdprof.h` (new, pure functions: class lookup with a rest slot,
  accounting, tick → µs), `v3da.h` (job stamps, the handler-shared `csd_done_cnt`, `v3da_cnt()`), `v3da_hw.c`
  (`csd_done_stamp` in `hw_service` and `v3da_hw_drain`; cleared on reset), `v3da_jobs.c` (stamps in `kick_csd`,
  accounting in the CSDDONE completion, `csdprof_print` from `stat_print`, `v3da_csdprof_enable`), `v3da_main.c`
  (`-C`). Default behaviour without `-C` unchanged apart from the stamp.
* **Host test:** `tools/gpu-lane/v3d-async/hosttest/csdprof_test.c`, run by `hosttest/run.sh` (native gcc,
  ASan + UBSan): v3dv-packed CFG words for the warp (64×64×1, wg 64), lightmap (2×1 and 16×32, one class),
  indirect (40×1) dispatches at 54 MHz; decode, per-class split, gpu/wake/pro/epi attribution, the no-IRQ and
  stale-stamp fallbacks, histogram edges (249/250/15 999/16 000 µs), the rest slot after 32 pipelines, CFG5 0 /
  0xffffffff. **`CSDHOST RESULT checks=23 fails=0 verdict=PASS`**; negative control (`-DCSDPROF_TEST_NEGCTL`,
  CSDDONE stamped at the event thread) **fails the attribution checks, as it must**. The lowmem test is
  unchanged (45/45, its negative control still fails).
* **Build:** `tools/gpu-lane/v3d-async/build.sh --out out-csdprof` (`-Werror`, 0 warnings), from this commit's
  tree = the current server (proto 5: G4 export, G6 implicit sync, lowmem scan-out) + the profile, against the
  sysroot's `libphoenix.a` `2acb195e2e089a49`. `out-csdprof/rpi4-v3d-async` **`055e7805bfa82d06…`**
  (1 229 680 B), `v3dasync-ping` `94df667c929bab43…`. Staged **`/bin/rpi4-v3d-async-csdprof`** on
  `/srv/phoenix-rpi4-nfs-gcc16` (`sudo -n install -m 755`, `cmp` OK). Nothing existing replaced:
  `/bin/rpi4-v3d-async-m3p2` (`ea131368…`), `/bin/vkq-drm-perf` (`ee0e3079…`) → `/usr/bin/vkquake-drm-perf`
  (`5fbf7899…`), `/bin/rpi4-kms-gate` (`0e8c127f…`) are untouched.

### 4. Pre-registered cycle `perf-vkq-e`

The `perf-vkq-b` command with the profiling server; same vkQuake binary, same kms.

```
./scripts/test-cycle-psh-interact.sh --label perf-vkq-e --wait-secs 220 --inter-cmd-secs 8 --idle-secs 60 \
    --max-cmd-secs 300 --ready-line 'V3DA srv detached|KMS srv detached' --ready-extra-secs 20 \
    --hdmi-dense-on 'vkquake-drm: new GPU lane' -- \
    "/bin/rpi4-v3d-async-csdprof -r 1 -m serial -i -C" \
    "/bin/rpi4-kms-gate -G" \
    "/bin/vkq-drm-perf"
```

**Confound vs `perf-vkq-b`:** the server base moves from `m3p2` (built 2026-09-27 00:50) to the current tree
(G4/G6/lowmem since), in the same binary as the profile; the vkQuake side is identical. If fps or the qstat
rows move by more than the tolerances below, rerun `perf-vkq-e` without `-C` (label `perf-vkq-e0`) before
reading anything into it: that separates the base drift from the instrument.

Grading as §6.2 (qstat deltas over the steady windows, frames from flipstat); the `csd` lines are cumulative,
so per-frame class numbers = the delta of a class's `n` / `gpu_us` over the same span.

| Line / quantity | `perf-vkq-b` | predicted `perf-vkq-e` | if instead… |
|---|---|---|---|
| `V3DA srv csdprof on cntfrq=54000000 …` | — | once, before `V3DA srv ready` | missing: `-C` not parsed / wrong binary (`cmp`) |
| `flipstat` fps (median, steady) | 17.27 | **17.3 ± 1.0** | < 16.3: base drift or instrument cost → `perf-vkq-e0` |
| CSD jobs / frame, ms / frame | 5.44, 19.46 | **5.4 ± 0.5, 19.5 ± 2** | outside: as above |
| **CSD ms / job (qstat)** | 3.58 | **3.6 ± 0.4** | — |
| Σ class `gpu_us` / qstat `csd` ms | — | **≥ 0.95** (`pro`, `wake` ≪ `gpu`) | `wake_us`/n ≥ 0.5 ms: completion latency *is* a cost (IRQ → event thread) — look at the event thread before anything else; `pro_us`/n ≥ 0.3 ms: the prologue (TLB/L2T) is, A/B `-k 0x01` (`V3DA_KNOB_TLB_ON_CHANGE`) |
| `noirq` | — | **0** (IRQ mode stamps every CSDDONE) | > 1 %: the handler misses CSDDONE (poll/drain stamps them late) |
| classes (distinct `cfg5`) | — | **4–5**: warp, lightmap, indirect clear, indirect draw, + X | 1 per dispatch (thousands of `cfg5`, rest slot fills): the key is not per pipeline |
| warp class | — | `wg=64x64x1 wgsz=64`, ≈ 2.2 jobs/frame, **gpu ≈ 2.7 ms/job** (h in the 2000–4000 bucket) | ≫ 2.7: the warp shader itself is slow → read its `thr4` (1-thread shader = spills/TMU stalls) |
| indirect clear / draw | — | ~1 job/frame each, `wgs ≤ 2` / ≈ numsurfaces/64, **< 0.5 ms/job** | ms-scale: the indirect pair is the unexplained cost |
| lightmap class | — | `wgs` varying (min..max over regions), ≈ 8 jobs/s, **≈ 4–5 ms/frame** in total | — |
| **X** | — | **one more class, ≈ 0.8 jobs/frame, ≈ 8 ms/frame**; if its `wg` is **240x135x1** it is the screen-effects pass | no fifth class: X was non-additivity between c and d (then the lightmap class carries it) |
| `V3DA srv qstat` err / wedges / rej | 0 / 0 / 0 | 0 / 0 / 0 | any: FAIL |
| exceptions | 0 | 0 | addr2line `out-csdprof/rpi4-v3d-async` |
| HDMI | lit, torches, "19 FPS" | same | — |

**What `perf-vkq-e` decides (next step, not yet taken):**
* X = **screen effects** (240×135): find which gate fires at the spawn view (`v_blend[3]` / `key_dest`) and,
  if nothing visible needs it, a vkQuake patch in the 0006/0007 pattern — up to ~8 ms/frame (≈ +2.5 fps).
* X = a class with a **small workgroup count at ~10 ms**: a slow shader, not a big dispatch — `thr4`/`seg1` and
  the shader's QPU count (`V3D_DEBUG=cs` on a host Mesa build of the same SPIR-V) next.
* `wake_us` or `pro_us` material: a server fix (event-thread priority/wake path, or per-job TLB flush) with its
  own A/B; otherwise the server is cleared for good.
* In any case the ~21 ms/frame of GPU idle at 64 % busy (§ short version) is the next CPU-side question: the
  hooks see no wait, so the time is in vkQuake's CPU frame or in an unhooked call.

## Result — `perf-vkq-e` (chain61, 2026-09-27 21:19, build 18 kernel): the compute row, by pipeline

Log `artifacts/rpi4b-uart/rpi4b-uart-20260927-211950-perf-vkq-e.log`; HDMI `artifacts/hdmi/20260927-212657-perf-vkq-e-tick.png`
(the spawn view, lit, torches, lava lit on the right). Server `/bin/rpi4-v3d-async-csdprof -C` (`055e7805…`), vkQuake
`/bin/vkq-drm-perf` (the perf-b binary). 0 exceptions, `err=0 wedges=0 rej=0`.

### The four classes

Steady state = the last 40 qstat windows (200 s, 3357 frames, 16.8 fps by flipstat totals); per class the delta of its
cumulative `V3DA srv csd` line over the same span. Identification by workgroup shape against vkQuake's dispatch sites
(§ "CSD per-job cost" §2) [measured + read]:

| `cfg5` | shape (last / min..max wgs) | `thr4` | vkQuake pipeline | jobs/frame | GPU ms/job | **GPU ms/frame** | histogram |
|---|---|---|---|---|---|---|---|
| `0x0b570005` | 64×64×1, 4096 always | 1 | **`cs_tex_warp`** (`gl_warp.c:130`, one per visible warp texture) | **3.06** | **4.77** (max 4.78) | **14.61** (75 %) | all 11 509 in 4–8 ms |
| `0x0b85f004` | 12×32×1, 384..2944 | **0** | **`update_lightmap`** (`r_brush.c:3490`, one per dirty lightmap region) | 0.47 (7.9/s) | **10.67** (max 21.5) | **5.04** (25 %) | 80 / 803 / 0 / 82 / 840 in 1–2 / 2–4 / 4–8 / 8–16 / ≥ 16 ms |
| `0x0b864005` | 87×1×1 | 1 | `indirect_draw` (`r_brush.c:3541`, `(numsurfaces + 63) / 64`) | 1.02 | 0.15 | 0.16 | all < 250 µs |
| `0x0b865005` | 2×1×1 | 1 | `indirect_clear` (`r_brush.c:3517`, 80 draws → 2 wgs) | 1.02 | 0.005 | 0.005 | all < 250 µs |
| **all** | | | | **5.57** | 3.56 | **19.8** | Σ gpu 74.78 s = **0.995** of qstat's `csd=75147ms` |

Per job, over all classes: prologue **3.2 µs**, wake (CSDDONE → event thread) **~10 µs**, epilogue (TMU/L2T clean)
**8–75 µs** (75 µs for the warp's 1 MB of image stores). `noirq=0` everywhere.

### Grading against §4's predictions

| quantity | predicted | measured | verdict |
|---|---|---|---|
| `csdprof on cntfrq=54000000` once | yes | yes | ✓ |
| fps (median, n = 45) | 17.3 ± 1.0 | **16.99** | ✓ (base drift m3p2 → current server: none visible) |
| CSD jobs, ms / frame; ms / job | 5.4 ± 0.5, 19.5 ± 2; 3.6 ± 0.4 | 5.43, 19.39; 3.57 (win.py span) | ✓ |
| Σ class gpu / qstat csd | ≥ 0.95 | 0.995 | ✓ — **the server adds nothing measurable per CSD job**: 3 + 10 + ≤ 75 µs against 4.8–10.7 ms |
| `noirq` | 0 | 0 | ✓ |
| classes | 4–5 | 4 | ✓ |
| warp | 64×64×1, wgsz 64, ≈ 2.2/frame, ≈ 2.7 ms/job | 64×64×1, wgsz 64, **3.06/frame, 4.77 ms/job** | ✗ — the b − d estimate was wrong (below) |
| indirect clear / draw | ~1/frame each, < 0.5 ms | 1.02 each, 0.005 / 0.15 ms | ✓ |
| lightmap | ≈ 8 jobs/s, 4–5 ms/frame | 7.9 jobs/s, 5.04 ms/frame | ✓ |
| X (a fifth class) | ≈ 0.8/frame, ≈ 8 ms/frame | **no fifth class** | the "no fifth class" branch: X was non-additivity. The warp class carries it, not the lightmap class the table guessed |
| err / wedges / rej, exceptions | 0 | 0 | ✓ |

**Why the decomposition missed.** `-c` (CPU lightmaps) is consistent with the profile: it left warp only, 2.99 jobs /
14.29 ms per frame (profile: 3.06 / 14.61). `-d` (raster warp) is not: it should have left lightmap + indirect,
≈ 2.5 jobs / 5.2 ms per frame, and it had **3.25 / 13.59**. At the profile's costs that remainder is the lightmap
class running **≈ 1.2 jobs/frame instead of 0.47** in that run [inferred: (13.59 − 0.16) / 10.67 = 1.26, and 3.25 − 2.04
= 1.21 jobs]. The lightmap update rate is set by which lightmap blocks were drawn (`lm->modified`) and which of their
light styles changed (`r_brush.c:3647-3732`), so it depends on the run, not only on the frame count. So `perf-vkq-d`'s
+1 fps undersold the raster warp: it removed 14.6 ms/frame of warp compute and, by chance or by cause, gained ~8 ms of
lightmap compute. Which of the two is what `perf-vkq-f` settles.

### Answers to the coordinator's questions

* **Why the 64×64 dispatch runs ~3×/frame:** it is one `cs_tex_warp` per warp texture flagged `update_warp` this frame
  (`gl_warp.c:157-196`: every texture of the world model with a turbulent surface in the drawn texture chains), each a
  full 512×512 redraw whatever its size on screen. At the spawn view that is three textures (lava is on screen at the
  right; the others are in the drawn set). 4.77 ms each = **18 ns, ~9 GPU cycles per texel** for a shader that does two
  `sin`, one texture sample and one `imageStore`: the dispatch is bound by TMU image writes (262 144 per texture, 1 MB),
  not ALU [inferred]. The raster path draws the same image through the tile buffer: `perf-vkq-d` measured +3 render
  jobs and **+1.9 ms/frame** of render for the three textures, i.e. ≈ 0.6 ms per texture against 4.8.
* **The `thr4=0` shader is `update_lightmap`.** It culls lights into shared memory behind three `barrier()`s and loops over
  light styles and lights per texel (`Shaders/update_lightmap.inc`, 365 lines); Mesa's v3d compiler falls back from 4
  threads to 2 or 1 when register allocation fails, and the barriers force one workgroup per supergroup (`sgwgs=1`).
  Making it 4-threaded is a shader rewrite (lower register pressure) or a compiler change, not a switch; the cheap levers
  are the dispatch rate (a run-dependent 0.47–1.2 per frame) and the CPU path (`r_gpulightmapupdate 0`, `perf-vkq-c`:
  +2.6 fps). `perf-vkq-f` also prints Mesa's shader statistics (`V3D_DEBUG=shaderdb`: instructions, threads, spills) so
  the thread count and spills are measured, not guessed.
* **Did the old lane run them?** Yes, both. The old glue wires `PCBX_UPDATE_WARP` and states that `R_UpdateWarpTextures`
  dispatches `cs_tex_warp` into it each frame (`phoenix-rtos-ports/vkquake/glue/pl_phoenix_vk_vid.c:116-124`), forces
  `r_gpulightmapupdate 1` (`glue/pl_phoenix_main.c:166`) and leaves `r_waterwarpcompute` at upstream's 1; its CSD path is
  the one compared above. So the old lane paid the same ~20 ms/frame of compute; what it did not pay is the new lane's
  ~21 ms/frame of GPU idle and the extra render passes (UI + post-process).

### The fix: raster warp on V3D (vkQuake patch 0008)

`tools/gpu-lane/sdl2-drm/patches-vkquake-perf/0008-gl_warp-raster-warp-by-default-on-phoenix.patch`: `r_waterwarpcompute`
defaults to **0** under `__phoenix__` (the 0006 pattern; the only Vulkan device there is V3D). The cvar stays
`CVAR_ARCHIVE`; `r_waterwarpcompute 1` still selects compute at run time (it is read every frame, `gl_warp.c:179-246`).
Kept out of `patches-vkquake/` until a cycle passes, as 0006/0007 were. *Done after `perf-vkq-f`: it is
`patches-vkquake/0008` (same number), `patches-vkquake-perf/` is gone again, see "Promotion" at the end.*

* **Build:** `VKQDRM_OUT=tools/gpu-lane/sdl2-drm/build-out/vkquake-drm-perf-f VKQDRM_TARGET=/usr/bin/vkquake-drm-perf-f
  VKQDRM_EXTRA_PATCHES=tools/gpu-lane/sdl2-drm/patches-vkquake-perf/0008-gl_warp-raster-warp-by-default-on-phoenix.patch
  tools/gpu-lane/sdl2-drm/build-vkquake-drm.sh` → rc 0, the script's proofs pass, 12 guarded shared files unchanged.
  `vkquake-drm.stripped` **`2cca8690e32455e7…`** (13 368 776 B), unstripped `65a0a4154ebeb1f9…`, launcher `vkq-drm`
  **`ae88fb5a3ea9b797…`** (execs `/usr/bin/vkquake-drm-perf-f`); patch set `c682d29b…` (0001–0007) + 0008; ICD
  `69c689ad…`, libdrm-phoenix m5b `a508e207…` unchanged. gdb on the unstripped ELF: `r_waterwarpcompute.string = "0"`,
  `r_oit.string = "0"`.
* **Confound:** it links the sysroot's `libphoenix.a` **`94a3e1e6…`** (build 19's printf quote-flag fix, installed
  21:27), where perf-b/-e's binary linked `2acb195e…`. A printf change is not expected to move GPU rows.
* **Staged** (`sudo -n install -m 755`, `cmp` OK): `/usr/bin/vkquake-drm-perf-f`, `/bin/vkq-drm-perf-f`. Untouched:
  `/usr/bin/vkquake-drm-perf` (`5fbf7899…`), `/bin/vkq-drm-perf` (`ee0e3079…`), `/bin/rpi4-v3d-async-csdprof`
  (`055e7805…`).

### Pre-registered `perf-vkq-f` (and `-f2`)

```
./scripts/test-cycle-psh-interact.sh --label perf-vkq-f --wait-secs 220 --inter-cmd-secs 8 --idle-secs 60 \
    --max-cmd-secs 300 --ready-line 'V3DA srv detached|KMS srv detached' --ready-extra-secs 20 \
    --hdmi-dense-on 'vkquake-drm: new GPU lane' -- \
    "/bin/rpi4-v3d-async-csdprof -r 1 -m serial -i -C" \
    "/bin/rpi4-kms-gate -G" \
    "export V3D_DEBUG=shaderdb" \
    "/bin/vkq-drm-perf-f"
```

`perf-vkq-f2` (only after f passes; no new binary): the same with `"/bin/vkq-drm-perf-f +r_gpulightmapupdate 0"`
(CPU lightmaps, which also turns off the indirect-draw compute) — the candidate for old-lane parity.

`V3D_DEBUG=shaderdb` only adds one Mesa line per compiled shader at pipeline creation (`mesa_logi("SHADER-DB-…")`,
`vir.c:2688`, before the first frame); steady-state rows are unaffected. `f` is the configuration of `perf-vkq-d`
(raster warp) with a compile-time default instead of a `+` command, so its fps is anchored on d; the profile says what
d's extra compute was.

| Line / quantity | `perf-vkq-e` | predicted `perf-vkq-f` | predicted `-f2` | if instead… |
|---|---|---|---|---|
| `vkq-drm: exec /usr/bin/vkquake-drm-perf-f …` | — | once | once, `+r_gpulightmapupdate 0` listed | stale launcher (`cmp`) |
| `SHADER-DB-… MESA_SHADER_COMPUTE shader: … threads …` | — | one per compute variant; the one with loops and the most instructions (update_lightmap) at **1 or 2 threads** and/or spills | same | no SHADER-DB lines: env not passed (`export` / launcher `execv`) — note, not a failure |
| warp class (64×64×1) | 3.06/frame, 14.6 ms/frame | **absent** (no `wg=64x64x1` line) | absent | present: the default did not take (config.cfg sets it?) |
| render jobs, ms / frame | 14.9, 16.7 | **≈ 17.9, ≈ 18.6** (d: +3 jobs, +1.9 ms) | same ± 1 | — |
| lightmap class jobs / frame, ms / frame | 0.47, 5.0 | **0.47–1.2, 5–13** | absent | > 1.3/frame: raster warp *causes* lightmap updates (find the `lm->modified` source) |
| indirect classes | 2 × 1.02/frame | same | **absent** | — |
| CSD ms / frame | 19.4 | **5–13** | **≈ 0** | ≥ 14: warp compute still on |
| GPU busy ms / frame | 36.9 | **24–32** | **≈ 19–20** | — |
| `flipstat` fps (median) | 16.99 | **18.3–22** (18.3 if d's lightmap rate recurs, ~22 if e's) | **21–25** | f < 17.5: no gain over e → the idle (~20 ms/frame) moved in; read waitstat |
| HDMI | lit, torches, lava | same; **water/lava/teleport surfaces textured and animated** (raster warp), not black or frozen | same, lightmaps lit (CPU path) | warp surfaces black/static: the raster path is broken on v3dv — reject 0008 |
| qstat err / wedges / rej, exceptions | 0 | 0 | 0 | any: FAIL |

**Decision:** f ≥ e + 1 fps with correct liquids → promote 0008 into `patches-vkquake/` (next free number) and rebuild the
default. f2 ≥ f + 1.5 fps with a correct picture → propose `r_gpulightmapupdate 0` as the Phoenix default (a 0009 in the
same pattern; note that it also disables GPU culling). If both land, the remaining gap to the old lane's 22.9 is the
~20 ms/frame of GPU idle, not compute.

## Result — `perf-vkq-f` / `perf-vkq-f2` (chain64, build 19, 2026-09-27 22:40 / 22:52)

Median over the whole run (111 fps lines each), same spawn view as `-e` (HDMI frames `…-perf-vkq-f-tick.png`,
`…-perf-vkq-f2-tick.png`, 23:01–23:03):

| run | change vs `-e` | fps median (min–max) | CSD | verdict vs pre-registration |
|---|---|---|---|---|
| e (baseline) | — | 16.99 | 19.4 ms/frame | — |
| **f** | patch 0008: raster water warp | **18.07** (11.2–18.1) | no 64×64 class ✓; lightmap class `0x0b85f004` n=5127 (was 1805) | ✓ fps in 18.3–22 band's low edge (+1.1 ≥ +1 rule) |
| **f2** | f + `r_gpulightmapupdate 0` | **29.70** (13.9–29.85) | **0 jobs** ✓ | ✓✓ above the 21–25 prediction; **old lane 22.9 beaten by 6.8 fps** |

- f2 sits at **exactly 30.00 fps** in the flipstat line, which is half of 60 Hz. That points to the swap waiting
  for every second vblank, so its real capacity is probably higher. Measure it with pacing off before quoting it
  as a ceiling.
- Picture: f2 and f frames of the spawn view are the same scene, lit, torches present, 0 exceptions.
  The spawn view shows no water surface, so the warp itself is not visible here.
- In f, the lightmap shader ran 2.8× as often as in e: with the warp gone, more frames per second each dispatch
  lightmap updates. That is why f gained only 1.1 fps, and it confirms the lightmap shader (single-threaded,
  `thr4=0`) as the other big cost.
- **Decision rules (pre-registered): both met.** Promote 0008 into `patches-vkquake/`, and make CPU lightmaps
  (`r_gpulightmapupdate 0`) the Phoenix default in a new patch. Then run `mig-vkq` on the promoted binary, with
  a water scene added to the grade. *Done: patches `0008` + `0009`, binary staged as `/bin/vkq-drm-g`, cycle
  `mig-vkq-g` pre-registered, the 30.00 explained: see "Promotion" below.*

## Promotion (2026-09-28, host only: build + staging, no Pi cycle yet)

### Patches

`tools/gpu-lane/sdl2-drm/patches-vkquake/`, applied in order by the default `build-vkquake-drm.sh`:

| patch | change | origin |
|---|---|---|
| **`0008-gl_warp-raster-warp-by-default-on-phoenix.patch`** | `r_waterwarpcompute` "0" under `__phoenix__` (CVAR_ARCHIVE, still switchable) | moved unchanged (`git mv`) from `patches-vkquake-perf/`, same number, as 0006/0007 were (commit `3c84ca17c`); `patches-vkquake-perf/` is gone again |
| **`0009-gl_rmain-cpu-lightmaps-by-default-on-phoenix.patch`** | `r_gpulightmapupdate` "0" under `__phoenix__` (CVAR_NONE as upstream, read every frame, so `r_gpulightmapupdate 1` still selects the GPU path at run time) | new, the 0006/0008 pattern |

0009's header gives the reason and the measurement: `update_lightmap` costs ~10.7 ms of GPU time per dispatch
(perf-vkq-e 10.67 ms, perf-vkq-f 5127 dispatches at 10.62 ms), and `+r_gpulightmapupdate 0` measured **+11.6 fps**
(f 18.07 → f2 29.70). **Wording correction:** the shader is not single-threaded. `thr4=0` means CSD CFG5's
4-thread bit is clear. The `V3D_DEBUG=shaderdb` lines of `perf-vkq-f` list every compute variant with loops
(432, 541 and 1341 instructions) at **2 threads, 0 spills**, and every 4-thread variant is one of the small
shaders (warp, indirect, v3dv's event/query shaders). So `update_lightmap` runs on 2 QPU threads, not 4. The
header also lists what upstream's CPU path turns off, because +11.6 fps is the net of all of it:
* the indirect-draw culling compute (`gl_rmain.c:1545`);
* vkQuake's frame tasks (`gl_screen.c:1503`, `use_tasks … && r_gpulightmapupdate.value`), so the CPU frame runs
  on one thread;
* lightstyle interpolation (`gl_rlight.c:67`): styles step at 10 Hz, as in classic Quake.

Dry run on a fresh tarball extraction: 0001–0009 apply with `patch -p1` (no fuzz, no offsets). Patch-file
sha256: 0008 `4ce0aafbd2013…`, 0009 `935a77cd86611…`; patch set stamp **`34a050962b1171a5`**.

### Build

```
VKQDRM_OUT=tools/gpu-lane/sdl2-drm/build-out/vkquake-drm-g VKQDRM_TARGET=/usr/bin/vkquake-drm-g \
  tools/gpu-lane/sdl2-drm/build-vkquake-drm.sh
```

* rc 0, 83 TUs, 0 warning lines, and the script's proofs pass (symbols, strings, call sites, old-lane
  inverse control, swap-order gate). 12 guarded shared files are unchanged, and 50 GB were free on `/`.
* Output files:
  * `vkquake-drm.stripped` **`15354b95b42f7c6b5a1566c2a0879a52f9c53671e1cea02500520a6bf010bce7`** (13 368 776 B);
  * unstripped `vkquake-drm` `2804c627e2be549ac9218884ded05ddd24b88557e1ff088ff8f08d714b7389ab` (use it for addr2line);
  * launcher `vkq-drm` **`22345b634bf71bae869d529307dee5ac467ae10e051c7fab69209b378055ddc1`** (execs
    `/usr/bin/vkquake-drm-g`).
* Inputs, all as in perf-f:
  * ICD `69c689ad4926672b` (Mesa patch set `60dd139d…`);
  * libdrm-phoenix m5b `a508e207…`;
  * libphoenix.a `94a3e1e6…`;
  * SDL source set `cd1e07eb499835db`.
* `libSDL2.a` hashes differently in every build of the same source set (`0c3c00e3…` here, `96ff7085…` in
  perf-f), so compare the source set, not the archive hash.
* BUILD-INFO says `sources git: DIRTY/untracked`. The cause is an uncommitted header edit by another agent in
  `build-vkquake-drm.sh`, not these patches.
* **Checks on the ELF** (`gdb-multiarch -batch`): `r_waterwarpcompute.string = "0"` (CVAR_ARCHIVE),
  `r_gpulightmapupdate.string = "0"` (CVAR_NONE), `r_oit.string = "0"`. `strings` on the launcher shows
  `/usr/bin/vkquake-drm-g` and `vkq-drm: exec /usr/bin/vkquake-drm-g … +map start`.
* **The engine binary does not depend on `VKQDRM_TARGET`**, which only reaches the launcher's `-D`
  (`build-vkquake-drm.sh:406`). So the swap after a Pi pass is to install these same
  `vkquake-drm.stripped` bytes as `/usr/bin/vkquake-drm`. The existing `/bin/vkq-drm` (`e49a7444…`) already execs
  that path. A default `build-vkquake-drm.sh` run (out dir `build-out/vkquake-drm`, not rebuilt here) now applies
  0001–0009 too.
* **Not updated, by choice:**
  * `build-vkquake-drm.sh`'s header still lists 0001–0007. That file carries another agent's uncommitted
    edit, so it is not part of this commit.
  * `scripts/check-gpu-lane-ports-sync.sh` (untracked, another agent's work) maps `patches-vkquake/` to
    `sources/phoenix-rtos-ports/vkquake_drm/patches`. That port directory does not exist in the checked-out
    ports tree yet, so when it lands it needs `0008` and `0009` copied in.

### Staged (`/srv/phoenix-rpi4-nfs-gcc16`, `sudo -n install -m 755`, both targets absent before, `cmp` OK)

| path | sha256 |
|---|---|
| **`/usr/bin/vkquake-drm-g`** | `15354b95b42f7c6b…` |
| **`/bin/vkq-drm-g`** | `22345b634bf71bae…` |
| untouched: `/usr/bin/vkquake-drm` | `22755bb450b09e0f…` (0001–0007) |
| untouched: `/bin/vkq-drm` | `e49a7444fc782d0f…` |
| untouched: `/usr/bin/vkquake-drm-perf-f`, `/bin/vkq-drm-perf-f` | `2cca8690…`, `ae88fb5a…` |

### Why f2 reads exactly 30.00 fps [read + measured]

**FIFO with two swapchain images, and a GPU frame of 19.9 ms, so every frame lands on the second vblank.** There is
no fps cap.

1. **FIFO is the only present mode.** Mesa's display WSI offers only FIFO
   (`wsi_display_surface_get_present_modes`, `wsi_common_display.c:1632`). vkQuake asks for IMMEDIATE/MAILBOX when
   `vid_vsync` is 0, its default (`gl_vidsdl.c:112`, `:2889-2905`), finds neither, and logs `Using FIFO present mode`.
2. **Two images.** `minImageCount = max(vid_vsync >= 2 ? 3 : 2, caps.minImageCount = 2)` (`gl_vidsdl.c:2928`,
   `wsi_common_display.c:1314`). The log imports exactly `kmsbuf id=1` and `id=2`.
3. **Acquire waits for a flip.** An image goes back to IDLE only when a *different* image's flip completes
   (`wsi_display_idle_old_displaying`, `wsi_common_display.c:1932-1945`, called from the flip handler at
   `:2066-2068`). With two images, `vkAcquireNextImageKHR` therefore returns at the vblank that shows the
   previous frame.
4. **The GPU starts only after acquire.** vkQuake submits the whole frame in one `vkQueueSubmit` after the
   acquire, waiting on its semaphore (`gl_vidsdl.c:4033` acquire, `:4200` submit, `:4234` present). So the GPU
   work of frame N cannot start before about the vblank where acquire returned.
5. **The GPU frame is longer than a vblank.** f2's steady state (last 40 qstat windows, 200 s, 6022 frames):
   * render 18.2 jobs and 18.95 ms per frame, bin 0.95 ms, CSD 0;
   * **GPU busy 19.9 ms/frame** (60 % of 33.3 ms), more than 16.7 ms even before the KMS server's
     `guard_us=2000` latch margin.
   So every frame misses the first vblank and flips on the second, and the next acquire returns there. The
   period is 33.3 ms, which is exactly 30.00 in every window.
6. **The waits fit.** `waitstat acquire` averages **10.8 ms** (median of 40 windows) and `present` 5.6 ms. That
   leaves 33.3 − 10.8 ≈ 22.5 ms per frame outside acquire, including present.
7. **What it is not:**
   * `host_maxfps` is 200 (`host.c:68`), which would pace at 5 ms, and no cfg on the export sets it,
     `vid_vsync` or any cvar above (`id1/autoexec.cfg`, `id1/config.cfg` read).
   * `vid_maxframelatency` / present-wait2 is inactive at `vid_vsync 0` (`gl_vidsdl.c:4023`; `pwait=0` in
     every waitstat line).
   * SDL `patches/0009` changes only the GL swap (`KMSDRM_GLES_SwapWindow`); vkQuake presents through the
     Vulkan WSI. It is the same problem, though: two buffers, and rendering that can start only at a vblank.
     0009's fix was likewise a third buffer.
8. **Why f did not lock to 20.00.** f's GPU frame was 33.2 ms, and its lightmap jobs vary from 1 to 21 ms. Its
   frames therefore mixed 3 and 4 vblank periods, averaging 18.1–18.3 fps.

**Capacity.** The GPU alone would allow ≈ 1000 / 19.9 ≈ **50 fps**. The CPU part outside acquire is ≈ 22.5 ms and
runs on a single thread under CPU lightmaps (no tasks), which allows ≈ **44 fps** if CPU and GPU fully overlap.
A true uncapped rate cannot be measured on this WSI: there is no IMMEDIATE or MAILBOX mode. A third image is the
closest proxy.

### Pre-registered `mig-vkq-g` — the promoted binary, the spawn view

The `perf-vkq-f` command, with the label and the launcher changed:

```
./scripts/test-cycle-psh-interact.sh --label mig-vkq-g --wait-secs 220 --inter-cmd-secs 8 --idle-secs 60 \
    --max-cmd-secs 300 --ready-line 'V3DA srv detached|KMS srv detached' --ready-extra-secs 20 \
    --hdmi-dense-on 'vkquake-drm: new GPU lane' -- \
    "/bin/rpi4-v3d-async-csdprof -r 1 -m serial -i -C" \
    "/bin/rpi4-kms-gate -G" \
    "export V3D_DEBUG=shaderdb" \
    "/bin/vkq-drm-g"
./scripts/check-torch-rois.py --label mig-vkq-g
```

Anchored on f2. The only intended difference is that the CPU path is on from the first frame: f2 set it after
`map start`, so its first frames used GPU lightmaps. Grade as §6.2: steady windows, qstat deltas, frames from
flipstat.

| Line / quantity | `perf-vkq-f2` | predicted `mig-vkq-g` | if instead… |
|---|---|---|---|
| `vkq-drm: exec /usr/bin/vkquake-drm-g -basedir … +r_rtshadows 0 +map start` | (perf-f + arg) | once, **no appended argument** | stale launcher / wrong staging (`cmp` against the shas above) |
| `Using R8G8B8A8 color buffer format (V3D: …)`, `Using FIFO present mode`, `kmsbuf id=1,2` | yes | same | — |
| `SHADER-DB-… MESA_SHADER_COMPUTE` lines | present (pipelines are built whatever the cvar) | same set, loop variants at 2 threads | — |
| `V3DA srv csdprof on …` | once | once | — |
| `V3DA srv csd cfg5=…` class lines / qstat `csd` | none / `0/0ms` | **none / `0/0ms`** | any class (e.g. the `thr4=0` one): the default did not take, so a cfg sets `r_gpulightmapupdate` or the binary is wrong |
| render jobs, ms / frame; bin ms | 18.2, 18.95; 0.95 | **18.2 ± 1, 19 ± 2; ≈ 1** | render ≫ 21 ms: raster warp or CPU-lightmap uploads cost more from frame 1 (diff the job counts) |
| GPU busy / frame | 19.9 ms | **20 ± 2 ms** | — |
| `flipstat` fps (median, steady) | 30.00 (run 29.70) | **30.00** in every steady window (run median 29.5–29.9) | < 29 steady: a regression against f2, diff the qstat rows; ≠ 30.00 upward: the pacing analysis above is wrong |
| `waitstat acquire` / `present` avg | 10.8 / 5.6 ms | **9–13 / 4–7 ms** | acquire ≈ 0: frames no longer wait for a flip, so the image count changed |
| HDMI | spawn view lit, torches, teleporter + lava sliver, "30 FPS" | same; the teleporter (a warp texture) drawn and animated by the raster path | teleporter black or frozen: raster warp broken, so reject 0008 |
| torch ROI check | — | PASS or inconclusive (viewpoint), as before | torches dark: a real finding |
| qstat err / wedges / rej, exceptions | 0 | 0 | any: FAIL; addr2line `build-out/vkquake-drm-g/vkquake-drm` |

**Decision:** if every row holds, swap `/usr/bin/vkquake-drm` to these bytes (see Build; `/bin/vkq-drm` stays).

### Pre-registered `mig-vkq-g-water` — a water scene

**Correction to a premise.** "vkQuake has no argv path for `+map`" is true of the **old** lane's `ports/vkquake`
only: its glue gives it none, and the boot map comes from the hand-staged `id1/phoenix-map.cfg`. vkquake-drm is
different in three ways:
* it is upstream `main_sdl.c` with patch 0001 (`common-publish-cmdline-on-shareware`), so `+` commands work on
  the shareware pak;
* its launcher passes `+map start` itself (`vkq-drm-launcher.c:41`) and appends extra arguments after it;
* f2's appended `+r_gpulightmapupdate 0` demonstrably took effect (0 CSD jobs).

A second `+map` therefore loads a second map after `start`.

**Which map.** A PVS scan of the shareware maps was run (scratch script; it is not a repo tool, and the numbers
are its output). It finds the `info_player_start` leaf, decodes its PVS, and keeps the warp faces in front of the
spawn view within a 16:9 90° FOV and 1500 units.
* `start` has `*lava1` (9 faces) and `*teleport` (2) in view. The f2 HDMI frame confirms the teleporter at the
  centre and a lava sliver bottom right, so raster warp is already on screen at the spawn view, only small.
  `*water1` (7 faces) is in the PVS but not in the frame, so it is occluded.
* **`e1m2`** has **12 `*04water1` faces at ≥ 574 units straight ahead** (spawn 1496 1664 296, yaw 270): the moat
  in front of the castle.
* Others: `e1m5` has `*04water2` + `*teleport`, and `e1m7` has `*lava1` (19 faces).

PVS does not model occlusion, so the HDMI frame is the check.

```
./scripts/test-cycle-psh-interact.sh --label mig-vkq-g-water --wait-secs 220 --inter-cmd-secs 8 --idle-secs 60 \
    --max-cmd-secs 300 --ready-line 'V3DA srv detached|KMS srv detached' --ready-extra-secs 20 \
    --hdmi-dense-on 'vkquake-drm: new GPU lane' -- \
    "/bin/rpi4-v3d-async-csdprof -r 1 -m serial -i -C" \
    "/bin/rpi4-kms-gate -G" \
    "/bin/vkq-drm-g +map e1m2"
```

Grade:
* the log shows e1m2's level name (`Castle of the Damned`, its worldspawn `message`) and then `entered the game`;
  steady windows are counted from that line. **`Introduction` will probably not print, and that is not a
  failure.** Both `map` commands run in the same `Cbuf_Execute` pass. `Host_Map_f` spawns the server and
  runs `connect local` synchronously (`host_cmd.c`), and the second `map` disconnects before the client has
  parsed `start`'s serverinfo, which is where the level name is printed (`cl_parse.c:1027`). `start.bsp`
  still loads, costing a few seconds over NFS;
* the HDMI ticks after it show a textured, animated water surface (the water region differs between two ticks
  while static walls do not), not black, flat or frozen;
* no `V3DA srv csd cfg5=0x…` warp class (64×64×1) appears;
* 0 exceptions and err/wedges/rej 0.

fps is recorded but not graded (a different scene). `check-torch-rois.py` does not apply, since its references
are for `start`.

### Pre-registered `pace-vkq-g` — how fast f2's configuration could go (no code change)

`vid_vsync` is one of `VID_Init`'s `read_vars`, and `CFG_ReadCvarOverrides` reads its `+` override from argv
**before** the swapchain exists (`gl_vidsdl.c:4704`; `VID_Restart` returns while `!vid_initialized`, `:4811`).
The later `stuffcmds` pass sets the same value, which is a no-op (`Cvar_SetQuick`, `cvar.c:477`). So
`+vid_vsync 2` gives a **3-image FIFO swapchain from the start, with no `VID_Restart`**. The WSI creates
exactly `minImageCount` images (`wsi_common_display.c:3413`). With `backend=plane`, each scan-out buffer is
carved from one contiguous pool (`kms_bo.c` `bo_from_pool`, `pool_mib=32` in the KMS ready line), and
3 × 8 298 496 B = 24.9 MB fits in 32 MiB. The ready line's `slots=3` counts firmware-framebuffer pan slots
(`kms_fw.c:310`), which this backend does not use. The command is `mig-vkq-g`'s with the launcher line
`"/bin/vkq-drm-g +vid_vsync 2"` and `--label pace-vkq-g`. Run it after `mig-vkq-g` passes.

| Line / quantity | `mig-vkq-g` (predicted) | predicted `pace-vkq-g` | if instead… |
|---|---|---|---|
| exec line | no argument | `… +map start +vid_vsync 2` | — |
| `Using FIFO present mode` | yes | yes (the only mode) | — |
| kmsbuf imports | `id=1,2` | **`id=1,2,3`** | two only: the override did not reach `VID_Init`; swapchain creation fails / no `flipstat` lines: the KMS pool could not give a third buffer (fragmentation, another client's BOs), a KMS-side limit, not vkQuake |
| `flipstat` fps (median, steady) | 30.00 | **38–50, not a constant 30.00** (CPU ≈ 22.5 ms suggests ~44; GPU caps at ~50) | exactly 30.00 again: the lock is downstream of the swapchain (`rpi4-kms -G` gate, or WSI `_wsi_display_queue_next`'s one flip in flight), so read the KMS server's flip stats; < 30: FAIL |
| `waitstat acquire` avg | ~11 ms | **< 3 ms** | — |
| `waitstat pwait` | 0 calls | **≈ 1 call/frame**, small avg: with `vid_vsync > 0`, `vid_maxframelatency 2` engages present-wait2, which the stack exposes (`VK_KHR_present_wait2` in the log) | pwait avg ≈ a vblank: the latency cap is the limiter, so rerun with `+vid_maxframelatency 0` (read every frame, no restart) |
| GPU busy / frame; busy share | 20 ms; 60 % | 20 ± 2 ms; **75–100 %** | busy share unchanged: the CPU frame is the limit; the next lever is CPU-side (tasks are off under CPU lightmaps) |
| HDMI, exceptions, err/wedges/rej | as mig-vkq-g | same, no tearing (FIFO) | — |
| `id1/config.cfg` on the export after the run | unchanged | **unchanged**: `vid_vsync` is CVAR_ARCHIVE, but the cycle kills vkQuake without a clean quit | contains `vid_vsync "2"`: restore it before the next cycle |

**What it decides:**
* ≥ 38 fps: propose `vid_vsync 2` (a 3-image swapchain) as the Phoenix default in a vkQuake patch
  (`gl_vidsdl.c`, the 0006 pattern), with its own cycle.
* A result near 44 rather than 50 confirms that the single-threaded CPU frame is the next wall. The follow-up
  would then be whether `r_tasks` can stay on with CPU lightmaps; upstream couples them at `gl_screen.c:1503`.

## Result — `mig-vkq-g`, `mig-vkq-g-water`, `pace-vkq-g` (chain68, build 21b, 2026-09-28 01:07–01:45)

| run | swapchain | fps median (deduplicated) | CSD | picture | verdict |
|---|---|---|---|---|---|
| **mig-vkq-g** (`/bin/vkq-drm-g`, promoted 0008 + 0009) | `kmsbuf id=1,2` | **29.71** (n=111) | **0 classes** | spawn view as f2, lit, counter "30 FPS" | ✓ as predicted |
| mig-vkq-g-water (`+map e1m2`) | id=1,2 | 29.64 (n=112) | 0 | `Castle of the Damned` loaded, `entered the game`; the spawn room shows **no water surface** | ✓ map; ⚠ the water check itself is **inconclusive** (no water on screen) |
| **pace-vkq-g** (`+vid_vsync 2`) | **`kmsbuf id=1,2,3`** | **44.21** (n=113, max 44.48) | 0 | same scene, counter "41 / 47 FPS", no tearing | ✓ inside the pre-registered 38–50; `waitstat acquire` avg **~32 µs** (was ~10.8 ms) |

- **The 30.00 fps was the two-image FIFO chain, as analysed**: a third image lifts vkQuake on the new lane to
  **44 fps, 1.9× the old lane's 22.9**.
- ⚠ **Measurement trap found here.** Two of these logs begin with 138 000–150 000 copies of the **previous** cycle's
  last `phxvk: run … fps=` line. This is the host serial tool replaying a stale buffer before the boot banner (memory
  "UART flood is a host capture artifact"), and it put the chain's first-pass median for pace-vkq-g at 29.82.
  Medians above are over `uniq`'d lines. The chain scripts' median must dedupe from now on.
- Next: make three images the new-lane default (launcher `+vid_vsync 2`, or `minImageCount` 3 in the WSI), then
  swap `/usr/bin/vkquake-drm` to the promoted bytes and re-run the migration row.
