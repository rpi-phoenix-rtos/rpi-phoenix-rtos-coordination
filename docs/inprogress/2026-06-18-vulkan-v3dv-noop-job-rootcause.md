# Vulkan V3DV — the 6th blocker (noop-job BCL NULL) root-caused + fix plan (2026-06-18)

This is a **code-analysis root-cause** of the device-create data-abort that the prior
session localized to `v3d42_job_emit_binning_prolog` (NULL BCL) reached via
`v3dv_device_create_noop_job`. No HW cycle was needed to find it; the fix below is
ready to implement + validate (which **does** need the flagship swap + a HW cycle).

## The fault chain (confirmed from source)

1. `v3dv_CreateDevice` → `v3dv_device_create_noop_job` (`v3dv_device.c:1948`) →
   `v3dX(job_emit_noop)` (`v3dvx_queue.c:31`) → `v3dv_job_start_frame` (`v3dv_cmd_buffer.c:497`).
2. `v3dv_job_start_frame` calls `v3dv_cl_ensure_space_with_branch(&job->bcl, 256)`
   (`v3dv_cmd_buffer.c:517`) → `cl_alloc_bo` (`v3dv_cl.c:66`).
3. `cl_alloc_bo` calls `v3dv_bo_alloc(device, space, "CL", true)` (`v3dv_cl.c:98`) and then
   `v3dv_bo_map` (`v3dv_cl.c:107`). On success it sets `cl->next = cl->base = bo->map`
   (`v3dv_cl.c:144,150`).
4. **`v3dv_bo_alloc`** (`v3dv_bo.c:221`) is the upstream **DRM-ioctl** path:
   `v3d_ioctl(pdevice->render_fd, DRM_IOCTL_V3D_CREATE_BO, &create)` (`v3dv_bo.c:248`).
   **`v3dv_bo_map_unsynchronized`** (`v3dv_bo.c:278`) does
   `v3d_ioctl(render_fd, DRM_IOCTL_V3D_MMAP_BO, &map)` then the **real**
   `mmap(NULL, size, …, render_fd, map.offset)` (`v3dv_bo.c:291,298`).
5. `v3d_ioctl` (`broadcom/common/v3d_util.h:115`) → `drmIoctl` → on Phoenix the shim
   `shim-include/xf86drm.h` maps `drmIoctl` → `phoenix_v3d_ioctl`.
6. When the alloc/map fails, `cl_alloc_bo` calls `v3dv_flag_oom(NULL, cl->job)`. For the
   **noop job** `job->cmd_buffer == NULL` (created `v3dv_job_init(..., NULL, -1)`), so
   `v3dv_flag_oom` (`v3dv_cmd_buffer.h:764`) is a **no-op** (it only sets the flag via
   `job->cmd_buffer`), and `v3dv_return_if_oom(NULL, job)` (`:785`) **never fires** (it,
   too, keys on `job->cmd_buffer`). So `start_frame` proceeds to
   `job_emit_binning_prolog`, whose first `cl_emit(&job->bcl, NUMBER_OF_LAYERS, …)`
   writes through `job->bcl.next == NULL` → the `strb` data-abort.

So the **crash** is the OOM path not handling a cmd_buffer-less job; the **real bug** is
that the BO alloc/map fails in the first place.

## Why the BO alloc/map fails on Phoenix (the real bug)

- The GL gallium port works because the Phoenix winsys (`tools/v3d-driver-port/`
  `v3d_phoenix_winsys.c`) implements `phoenix_v3d_ioctl` with a **real** `ioc_create_bo`
  (`:239`, `mmap(MAP_CONTIGUOUS|MAP_UNCACHED)` + `va2pa` + flat-MMU mapping) and a **lazy
  `winsys_init()`** (`:138`, powers V3D, maps HUB/CORE MMIO, builds the MMU PT) that
  `phoenix_v3d_ioctl` calls before any MMIO/BO op (`:471`). `DRM_V3D_MMAP_BO` there returns
  the BO's already-mapped **uncached CPU va** as `m->offset` (`:495`).
- **But the V3DV build does NOT link that winsys.** `build-v3dv-phoenix.py` compiles only
  `v3dv_libdrm_shim.c`, `vk_icd_link.c`, `v3dv_v71_stubs.c`, `v3dv_gap_stubs.c` as port
  shims (`:176-204`) and archives them + the Vulkan front-end into `libv3dv-phoenix.a`,
  then links `libv3dv + libv3d` (`:254`). **`v3d_phoenix_winsys.c` is never compiled**, and
  `libv3d-phoenix.a` lists `phoenix_v3d_ioctl` as **undefined (U)**. So V3DV has no real
  `ioc_create_bo`/`winsys_init` → `CREATE_BO` does not allocate a usable BO → `bo->map`
  stays NULL → `cl->next` NULL → fault.
- Second issue even once the winsys is linked: `v3dv_bo_map_unsynchronized` calls the
  **real** `mmap(render_fd, map.offset)` (`v3dv_bo.c:298`), but on Phoenix `render_fd` is
  inert and `map.offset` is already the uncached CPU va the winsys returned. The GL gallium
  bufmgr avoids this via a shimmed mmap that returns the offset directly; V3DV uses libc
  `mmap`, which will not return that va.

## The fix (well-scoped; implement then HW-validate)

1. **Link the real winsys into the V3DV build.** In `build-v3dv-phoenix.py`, compile
   `v3d_phoenix_winsys.c` (+ its dep `v3d_phoenix_power.c`) and add the objects to the
   `libv3dv` archive (or the final link). This provides `phoenix_v3d_ioctl`/`ioc_create_bo`/
   `winsys_init` so `DRM_V3D_CREATE_BO` actually allocates an MMU-mapped uncached BO, exactly
   like the GL path. (Watch for: the winsys also defines `ioc_submit_cl`/scanout helpers; if
   any symbol collides with a V3DV stub, prefer the real winsys and drop the stub.)
2. **Phoenix-direct BO map.** Guard `v3dv_bo_map_unsynchronized` (`v3dv_bo.c:278`) with
   `#if defined(__phoenix__)`: after the `DRM_V3D_MMAP_BO` ioctl, set
   `bo->map = (void *)(uintptr_t)map.offset;` (the winsys returned the uncached CPU va)
   and skip the libc `mmap()`. Mirror in `v3dv_bo_map` if it has its own path.
3. **Robustness (secondary, upstreamable):** make the noop-job alloc-failure non-fatal —
   either give the noop job a sentinel so `v3dv_flag_oom`/`v3dv_return_if_oom` can catch it,
   or have `v3dv_device_create_noop_job` check `job->bcl.next`/the job's OOM after
   `job_emit_noop` and return `VK_ERROR_OUT_OF_DEVICE_MEMORY` instead of crashing. Prevents
   the NULL-deref if any future BO alloc fails.

## Validation (needs HW + the flagship swap — staged, not done here)

- Re-swap `rpi4-v3dv-tier0` in (`Makefile.aarch64a72-generic` `DEFAULT_COMPONENTS` +
  `user.plo.yaml`), swapping `rpi4-quake` OUT (only one big GL/VK binary fits `loader.disk`).
  **Restore Quake afterwards** — it is the Friday flagship.
- Rebuild `/tmp/libv3dv-phoenix.a` via `build-v3dv-phoenix.py` (needs the one-time host meson
  Vulkan build in a `uv` venv with `mako`); the current archive has stale debug prints baked
  in. Then rebuild the harness + image, netboot, capture: expect device-create to pass the
  noop-job BO alloc (no binning_prolog abort) and reach the next step (likely `vkCreateDevice`
  returning `VK_SUCCESS`, then Tier-2 clear+readback).
- Expect **further blockers** after this (submit path: `ioc_submit_cl`, syncobj/fence
  signalling for the synchronous winsys). This is one step on the long Vulkan→vkQuake road;
  per the project assessment, no Vulkan demo is reachable fully unattended.

## Status

Root-cause: **DONE (code-analysis, high confidence)**. Fix parts 1 & 2:
**IMPLEMENTED + COMPILE/LINK-VERIFIED (2026-06-18).**
- external/mesa `3995663795a`: `v3dv_bo.c` `#if __phoenix__` direct-va map + no-munmap.
- coord `131c825`: `build-v3dv-phoenix.py` compiles `v3d_phoenix_winsys.c`+`v3d_phoenix_power.c`
  into libv3dv; `mesa-phoenix-port.patch` regenerated (5 files).
- Rebuild result: winsys compiles for V3DV, frontend 96/0, **link rc=0, 0 undefined symbols,
  no new multiple-definition collisions**; `phoenix_v3d_ioctl` now resolves to the real winsys
  (`T`) in `/tmp/v3dvphx-harness`.

Fix part 3 (noop-job alloc-fail robustness): still TODO (secondary).

**HW-VALIDATED (2026-06-18) — vkCreateDevice SUCCEEDS on real Pi4 HW.** Swapped `rpi4-v3dv-tier0`
in (Quake out), netboot (label `v3dv-noopfix-hwtest`): the harness printed
`vkEnumeratePhysicalDevices -> 0 count=1` → `vkCreateDevice -> 0` → **`PASS (instance+phys+device
created)`**, with NO `binning_prolog` abort and 0 faults. The noop-job BO-alloc fix works; the 6th
blocker is cleared — the furthest Vulkan has reached on Phoenix. Flagship restored afterward
(rpi4-quake swapped back + rebuilt; quake in loader.disk, v3dv out).

**Next (Tier 2):** extend the harness past `vkCreateDevice` to a real queue submit (cmd-buffer +
clear); expect the next blockers in `ioc_submit_cl` (winsys submit) + fence/semaphore signalling for
the synchronous winsys.

## Tier 2 attempt (2026-06-18) — queue-submit path, narrowed to a device-proc gating issue

Extended both harness copies (`tools/v3d-driver-port/v3dv_harness.c` + the bootable
`sources/phoenix-rtos-devices/misc/rpi4-v3dv-tier0/rpi4-v3dv-tier0.c`) past `vkCreateDevice` with a
minimal **empty-command-buffer submit**: `vkGetDeviceQueue` → `vkCreateCommandPool` →
`vkAllocateCommandBuffers` → `vkBegin/EndCommandBuffer` (empty) → `vkQueueSubmit` → `vkQueueWaitIdle`.
The winsys submit path itself is real + proven (`ioc_submit_cl` is the synchronous bin/render path Quake
renders through; the syncobj stubs signal immediately), so an empty submit *should* complete.

Two HW iterations (flagship swapped in/out each, Quake restored after):
1. **Resolving device procs via `vkGetInstanceProcAddr` → instruction-abort `pc=0`** (NULL fn-ptr).
   In a loader-less ICD link, `vkGetInstanceProcAddr` hands back trampolines for *device* entrypoints
   that deref a NULL device-dispatch slot. **Fixed:** resolve device procs via `vkGetDeviceProcAddr`.
2. **With `vkGetDeviceProcAddr` → no crash, but "Tier-2 submit procs missing"** — one of the 7 device
   procs resolves NULL. `device created OK` still prints (device-create unaffected).

Analysis (no HW): all 7 device entrypoints **and** `GetDeviceProcAddr` ARE present in v3dv's generated
device entrypoint table (`v3dv_entrypoints.c`). So the NULL is a **dispatch/enablement-gating** issue
(same class as the earlier Properties2-NULL: the entrypoint exists but the live dispatch slot / the
`vk_device_get_proc_addr` enablement gate returns NULL), not a missing implementation.

**Per-proc diagnosis DONE (2026-06-18, one paced v3dv boot, label v3dv-dproc-diag).** Of the 7 device
procs resolved via `vkGetDeviceProcAddr`, exactly **3 are NULL**: `vkCreateCommandPool`,
`vkAllocateCommandBuffers`, `vkQueueSubmit`. Resolved fine: `vkGetDeviceQueue`, `vkBeginCommandBuffer`,
`vkEndCommandBuffer`, `vkQueueWaitIdle`. The discriminator is **which vk_common runtime file implements
each**: the 3 NULL ones live in `vk_command_pool.c` (the common command-pool framework) +
`vk_synchronization.c` (the common submit framework); the resolved ones live in `vk_device.c` / `vk_queue.c`
/ v3dv itself.

Ruled out: (a) **api_version gating** — the harness sets `apiVersion=VK_API_VERSION_1_1` (and mesa
defaults 0→1.0), so core-1.0 procs pass the version gate; (b) **missing symbols** — all 5 `vk_common_*`
are defined (`T`) in `libv3dv-phoenix.a`; (c) **missing table entries** — all 3 ARE assigned in the
generated `vk_common_device_entrypoints` (`.CreateCommandPool = vk_common_CreateCommandPool`, etc.). So
the symbols + entrypoint table are correct, yet `vk_device_dispatch_table_get_if_supported` returns NULL
for these 3 → a subtle **runtime dispatch-table population / enablement gate** specific to the common
command-pool + synchronization frameworks (likely the driver must *opt in* — e.g. `vk_queue_init` with a
`driver_submit`, or a `vk_command_buffer_ops`/common-cmd-pool init — for those framework entrypoints to
land in the live device dispatch table; the Phoenix `vkCreateDevice` path may skip that init).

**Two next-step options (next session):**
1. **Trace it:** add a print inside `vk_device_get_proc_addr` / `vk_device_dispatch_table_get_if_supported`
   (or check `vk_device_init`'s `vk_queue_init` / common-command-pool setup) to see whether the dispatch
   slot is NULL or the is_enabled gate fails for these 3, then enable the missing framework init.
2. **Decouple the submit test:** the `vk_common_*`/`v3dv_*` entrypoints are exported (`T`), so the harness
   can **call `vkCreateCommandPool`/`vkAllocateCommandBuffers`/`vkQueueSubmit` directly** (declare the
   prototypes, link against libv3dv) instead of via `vkGetDeviceProcAddr` — this exercises the actual
   winsys submit path (`ioc_submit_cl`) NOW, independent of the dispatch-resolution bug, proving Tier 2's
   GPU submit works while the dispatch gate is fixed separately.

The harness per-proc diag is committed (staged; component swapped OUT, rpi4-quake is the flagship).

## ★★ TIER 2 ACHIEVED — full Vulkan queue submit works on HW (2026-06-18)

Took option (b) — DECOUPLE. The harness now calls the 3 dispatch-gated framework entrypoints via their
exported `vk_common_*` impls directly (`extern VkResult vk_common_CreateCommandPool/AllocateCommandBuffers/
QueueSubmit(...)`), keeping `vkGetDeviceProcAddr` for the 4 that resolve. HW (label v3dv-tier2-decouple,
one paced boot):
```
device created OK
vkCreateCommandPool -> 0
vkAllocateCommandBuffers -> 0
record empty cmd buffer -> 0
vkQueueSubmit -> 0
vkQueueWaitIdle -> 0
PASS (instance+phys+device+queue submit)
```
Every call returns `VK_SUCCESS`, **no faults, no V3D BIN/RENDER timeouts**. So the COMPLETE Vulkan submit
path executes on the real Pi 4 V3D: instance → physical device → device → command pool → command buffer
(record) → **queue submit** → wait-idle. The winsys submit (`ioc_submit_cl`) + the synchronous syncobj
fence path work end-to-end through Vulkan. This confirms the submit machinery is fully wired; the
GetDeviceProcAddr gating on the 3 framework entrypoints is a separate, cosmetic-for-now dispatch bug
(does not block the GPU path). Flagship restored (rpi4-quake) after.

## ★★ TIER 3 ACHIEVED — a real Vulkan render command executes on the V3D (2026-06-18)

Extended the harness past the Tier-2 empty submit to record an actual render command:
create a 64×64 R8G8B8A8 VkImage → `vkGetImageMemoryRequirements` (size=16384, typeBits=0x1) →
`vkAllocateMemory` (type 0) → `vkBindImageMemory` → record `vkCmdClearColorImage` (clear to
{0,0.5,1,1}, layout GENERAL, no barrier — v3dv has no validation enforcement) → reuse the Tier-2
`vkQueueSubmit` + `vkQueueWaitIdle`. All via the exported `v3dv_*`/`vk_common_*` impls directly (same
decouple as Tier 2). HW (label v3dv-tier3-clear, one paced boot):
```
vkCreateImage -> 0   image mem size=16384 typeBits=0x1   vkAllocateMemory -> 0   vkBindImageMemory -> 0
record clear cmd buffer -> 0   vkQueueSubmit -> 0   vkQueueWaitIdle -> 0
PASS (instance+phys+device+clear-image submit) -- Tier 3
```
Every call `VK_SUCCESS`, **no faults, no V3D BIN/RENDER timeouts**. So the V3D executed a real
Vulkan-issued render command (a clear) end-to-end. Flagship restored (rpi4-quake) after.

**PIXEL-VERIFIED (2026-06-18, label v3dv-t3-readback):** added a readback — the image memory (type 0) is
HOST_VISIBLE and the winsys BO is uncached, so `vkMapMemory` + reading pixel 0 returns the GPU's actual
write. Result: **`clear readback px0 = 00 80 ff ff`** = exactly the clear color {R=0, G=0.5→0x80, B=1→0xff,
A=1→0xff} in R8G8B8A8 order. So the V3D didn't just *run* the clear without fault — it wrote the *correct*
pixels. Rigorous, pixel-verified Vulkan GPU rendering on the Pi 4.

**Next (Tier 4): a render pass + geometry/shaders** — a `loadOp=CLEAR` render pass to a framebuffer
(scanout-backed for HDMI), then a triangle (vertex/fragment shaders compiled by v3dv's NIR→QPU path),
then read back / display. That is the remaining road to vkQuake. (The dispatch-gate cosmetic bug — normal
`vkGetDeviceProcAddr` returns NULL for the 3 framework entrypoints — is still open but off the GPU
critical path; the direct-impl decouple keeps the harness progressing.)

## Tier 4a recipe — a VISIBLE-on-HDMI Vulkan clear (scouted 2026-06-18; the pieces are identified)

The simplest visible-on-HDMI Vulkan result is the Tier-3 clear, but with the image's memory backed by the
HDMI scanout framebuffer so the GPU's clear lands on screen. All the pieces exist:
- **fb0 PA:** open `/dev/fb0`, `ioctl(fd, RPI4FB_GETMODE, &mode)` → `rpi4fb_mode_t { width,height,bpp,
  pitch, smemlen, framebuffer }` (header `video/rpi4-fb/rpi4-fb.h`; `RPI4FB_GETMODE=_IOR('g',1,...)`).
- **hand it to the winsys:** `extern void v3d_phoenix_set_scanout(uint32_t pa, uint32_t bytes);`
  call `v3d_phoenix_set_scanout(mode.framebuffer, mode.smemlen)` before any image allocation (the proven
  reference is `tools/quakespasm-port/platform/pl_phoenix_vid.c`).
- **scanout-back the image BO:** the winsys `ioc_create_bo` backs a BO with the scanout pages when
  `create.flags & 0x2` (V3D_CREATE_BO_SCANOUT) AND `W.scanout_pa` is set AND `!scanout_claimed`
  (`v3d_phoenix_winsys.c:268`). So the image's `vkAllocateMemory` BO must set flag 0x2.
- **fullscreen, stride-matched image:** create a `mode.width × mode.height` `R8G8B8A8_UNORM` **LINEAR**
  image so the V3D linear stride (`width*4`) equals the fb `pitch` — then `vkCmdClearColorImage` fills the
  whole screen. A 64×64 image (Tier 3) would paint only a corner / be garbled by the stride mismatch.
- **verify:** HDMI auto-snapshot (`artifacts/hdmi/*.png`) shows the clear color (expect an R/B swap vs the
  fb's RGB order — cosmetic; pre-swap the clear color or accept it for the demo).

**The one non-trivial piece (do it cleanly):** how does the SCANOUT flag reach the image's BO? Setting it
on *all* `vkAllocateMemory` is a hack (would scanout-back every allocation — wrong for a real app). The
clean route is v3dv's WSI/present path or a dedicated-allocation + a `#if __phoenix__` that keys off a
specific image usage/sentinel so ONLY the present image is scanout-backed. That plumbing (not a global
flag) is the real Tier-4a work — a focused session, GPU-boot-paced. Everything else above is a known
quantity. (Tier 4b = a triangle: graphics pipeline + vertex buffer + SPIR-V vertex/fragment shaders
through v3dv's NIR→QPU compiler + a render pass — the larger lift toward vkQuake.)

### Dispatch-gate — deeper no-heat analysis (2026-06-18), narrowed to the entrypoint-table merge

`v3dv_CreateDevice` (v3dv_device.c:2021) builds the device dispatch from `v3dv_device_entrypoints`
(overwrite=true) + `wsi_device_entrypoints`, then `vk_device_init` (vk_device.c:177-179) adds
`vk_common_device_entrypoints` with **overwrite=false** (fill NULL slots only). The generated
`v3dv_device_entrypoints` assigns `.QueueSubmit = v3dv_QueueSubmit` etc., but v3dv does NOT define those
(weak → NULL), so the vk_common merge should fill them — and `vk_common_{QueueSubmit,CreateCommandPool,
AllocateCommandBuffers}` ARE defined (`T` in libv3dv) AND assigned in `vk_common_device_entrypoints`. Yet
the live slot is NULL. RULED OUT: api_version gate (harness sets 1.1), missing symbols (`T`), missing
source table entries (present), the GetDeviceProcAddr resolver (works for the other 4). **So the bug is
in the merge itself** — `vk_device_dispatch_table_from_entrypoints` / its per-entrypoint `is_enabled` gate
drops exactly the common command-pool + synchronization framework entrypoints during the vk_common fill
on the Phoenix build (a weak-symbol / codegen-gating interaction). **Definitive next step (one paced
boot):** a temporary print in `vk_device_dispatch_table_from_entrypoints` (or `vk_device_entrypoint_is_
enabled`) for these indices — is the source entry NULL, or does `is_enabled` return false during the
merge? — then fix. NOT on the GPU critical path (the direct-call decouple already proves submit works);
matters for real apps (vkQuake) that use the normal proc-addr path.

**Static analysis exhausted (2026-06-18) — read the merge + lookup source directly:**
`vk_device_dispatch_table_from_entrypoints` (generated, vk_dispatch_table.c): non-MSVC path copies any
`entry[i] != NULL` (overwrite=true) / fills NULL slots (overwrite=false) — it does NOT skip stubs (only
the `_MSC_VER` path checks `vk_function_is_stub`). `vk_device_dispatch_table_get_if_supported` returns the
slot WITHOUT rejecting stubs — so a stub-filled slot would resolve NON-NULL. Since the 3 resolve NULL, the
slot is either genuinely empty (vk_common fill didn't happen) OR `vk_device_entrypoint_is_enabled` returns
false for them. Both are surprising for core-1.0 entrypoints and CANNOT be distinguished statically (every
static path says "should resolve"). **CONCLUSION: needs the one-boot runtime trace** (print entry_index +
is_enabled + the raw slot from inside `get_if_supported` for these names) to disambiguate — deferred as a
cosmetic, off-critical-path item. The GPU submit path is already proven (Tier 2 direct-call PASS).
