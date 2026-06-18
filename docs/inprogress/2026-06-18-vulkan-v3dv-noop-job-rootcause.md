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

**Next-session diagnostic (precise):** add per-proc NULL logging to the harness (print each of the 7
results) to identify WHICH proc(s) are NULL; then trace `vk_device_get_proc_addr` /
`vk_device_dispatch_table_get_if_supported` gating (core-version / enabled-features) in
`vk_device_init` for the Phoenix device-create path — likely the same "passed NULL where a real
struct was expected" pattern that left Properties2 NULL. The harness Tier-2 code is committed (staged;
component stays swapped out, Quake is the flagship).
