# Porting Mesa V3DV (Vulkan for Broadcom V3D) to Phoenix-RTOS, toward vkQuake

> **STATUS (2026-06-26): plan substantially executed; PAUSED at one well-localized gap.**
> The V3DV ICD + Vulkan runtime + spirv_to_nir compile/link for aarch64-phoenix (Tier 0,
> done), Vulkan fully initializes on real HW (instance/device/queue/41 real-SPIR-V shaders),
> the frame loop / present / projection / cull are fixed, and a 2D GPU quad renders on the
> V3D (HW-proven). The single remaining blocker is the **no-WSI winsys texture upload**
> (`DRM_V3D_SUBMIT_TFU` no-op + CL meta-copy fallback don't land textures) → a focused
> buffer→image-copy session in `v3d_phoenix_winsys.c`. Task #29, PAUSED; GL Quake flagship
> unaffected. Current detail: `2026-06-23-vkquake-port-scaffold-status.md`.

**Date:** 2026-06-16
**Type:** RESEARCH + IMPLEMENTATION PLAN (no source changes; design only)
**Scope:** Phoenix-RTOS RPi4 port (BCM2711, Cortex-A72, V3D 4.2). Targets a working
`external/mesa/src/broadcom/vulkan/` (V3DV) ICD on Phoenix, then vkQuake.

> **TL;DR verdict.** This is **feasible but it is a months-scale effort, not weeks.**
> The single most important finding is *reassuring*: **a synchronous, single-in-flight
> submit model is conformant Vulkan, not a hack.** The GL path already proves the
> exact HW + winsys submit primitive V3DV needs (`drm_v3d_submit_cl` is the *same
> struct* the gallium driver uses, and our winsys `ioc_submit_cl` already consumes it).
> So Vulkan does **not** fundamentally need async submission or IRQ-driven fences to be
> *correct* — it needs them only to overlap CPU and GPU for higher fps. We will not
> match Linux's 60fps@1080p (single frame in flight), but the GL precedent (42fps@1080p)
> says playable is reachable.
>
> The real cost is **labor + one-bug-at-a-time correctness**, concentrated in three
> places: (1) getting the whole `src/vulkan/runtime` + `spirv_to_nir` closure to
> *compile and link* under libphoenix (a bigger link than the GL frontend, and it needs
> a C11-threads `mtx/cnd/thrd` shim over `sys/threads.h`); (2) the texture-upload / copy
> path, where V3DV emits **TFU jobs our winsys currently silently drops** (returns
> success, does nothing → missing textures, near-undiagnosable); (3) a custom
> swapchain/WSI to `/dev/fb0`, where **vkQuake is more tightly coupled to
> acquire/present semantics** than quakespasm was to the GL vid layer.
>
> None of those three blocks the *architecture*. The synchronous model is sound.

---

## 0. How this document is grounded

Every claim below is tied to a file I read in this repo. The two anchors are:

- The working GL port: `tools/v3d-driver-port/` — especially
  `v3d_phoenix_winsys.c` (the fake-DRM-V3D kernel), `v3d_libdrm_shim.c` (the
  `drmSyncobj*`/PRIME surface), `build-v3d-phoenix.py` (the cross-build recipe).
- The V3DV source: `external/mesa/src/broadcom/vulkan/*.c` and the Vulkan runtime
  `external/mesa/src/vulkan/runtime/*.c`, plus the UAPI
  `tools/v3d-driver-port/v3d_drm.h` (Mesa's vendored copy is identical).

Reference-only: `external/linux/drivers/gpu/drm/v3d/` for what the real kernel does
(IRQ/sched/TFU), and `external/mesa/.../src/broadcom/{cle,compiler,qpu}` which V3DV
*shares* with gallium and which we have **already built** for the GL port.

---

## 1. V3DV architecture vs. the gallium v3d driver — what's new for the "kernel"/winsys

The GL path and V3DV are two front-ends over the **same** Broadcom back-end. The
back-end (NIR→QPU compiler `v3d_compile`, the CLE packers from `gen_pack_header`, the
QPU encoder, perfcntrs) is shared and **already cross-compiled** into
`/tmp/libv3d-phoenix.a` by `build-v3d-phoenix.py`. That is the biggest single reuse win
and it de-risks all of "can the V3D shader/CL machinery run on Phoenix" — it already does.

What differs is the *front-end's contract with the kernel*. The kernel contract is
expressed entirely as `v3d_ioctl(render_fd, DRM_IOCTL_V3D_*, arg)` calls plus the
`libdrm` surface (`drmSyncobj*`, `drmGetDevices2`, `drmPrime*`). Mapping each thing
V3DV needs to what the winsys provides today:

| V3DV need | DRM ioctl / libdrm call | Winsys status (`v3d_phoenix_winsys.c` / `v3d_libdrm_shim.c`) |
|---|---|---|
| BO alloc | `DRM_V3D_CREATE_BO` | **Done.** `ioc_create_bo` (mmap+va2pa, flat MMU PTE). Same struct. |
| BO CPU map | `DRM_V3D_MMAP_BO` | **Done.** `ioc_mmap_bo` returns the already-mmap'd va as offset. |
| BO GPU addr | `DRM_V3D_GET_BO_OFFSET` | **Done.** Returns `b->gpuva`. |
| BO free | `DRM_IOCTL_GEM_CLOSE` | **Done.** `ioc_close_bo` reclaims slot + GPU VA. |
| device info | `DRM_V3D_GET_PARAM` | **Mostly done.** Real V3D-4.2 IDENTs. **Must add** `SUPPORTS_MULTISYNC_EXT`, `SUPPORTS_PERFMON`, `SUPPORTS_CPU_QUEUE` (see §1.2). |
| **graphics submit** | `DRM_V3D_SUBMIT_CL` | **Done, reusable as-is** — see §1.1. The single most important reuse. |
| **multi-sync extension** | `drm_v3d_multi_sync` via `DRM_V3D_SUBMIT_EXTENSION` | **New, but ignorable.** Winsys reads `bcl/rcl/qma/qms/qts` and never touches `submit.extensions`; the chained `in/out_syncs` are no-ops because submit is synchronous (§2). |
| **texture xfer** | `DRM_V3D_SUBMIT_TFU` | **DANGER: currently silently dropped** (default case returns 0). Correctness hole for Tier 5. See §1.3. |
| **compute** | `DRM_V3D_SUBMIT_CSD` | No-op today. vkQuake uses no compute → safe to leave stubbed. |
| **CPU jobs** | `DRM_V3D_SUBMIT_CPU` | No-op today. Used for queries/timestamps/indirect-CSD. vkQuake (classic Quake) issues none → safe. See §1.4. |
| sync objects | `drmSyncobjCreate/Destroy/Wait/...` | **Partial.** Stubs exist for Create/Destroy/Wait/Import/Export; **missing Signal/Reset/Query + timeline variants** that `vk_drm_syncobj` calls. See §2. |
| device enum | `drmGetDevices2` → `open(node)` | **MISSING ENTIRELY.** GL never enumerated; the harness handed gallium an fd. Hard Tier-1 gate. See §1.5. |
| perf queries | `DRM_V3D_PERFMON_*` | No-op. Not needed. |
| BO sharing | `drmPrime*` | Stubbed to fail. WSI wants it; we bypass WSI (§3). |

### 1.1 SUBMIT_CL is the same struct — the headline reuse

`handle_cl_job` in `v3dv_queue.c:797` builds a `struct drm_v3d_submit_cl` and fills
exactly the fields the winsys already reads:

```
submit.bcl_start = ...; submit.bcl_end = ...;   // CT0 QBA/QEA
submit.rcl_start = ...; submit.rcl_end = ...;   // CT1 QBA/QEA
submit.qma = job->tile_alloc->offset;           // CT0 QMA
submit.qms = job->tile_alloc->size;             // CT0 QMS
submit.qts = job->tile_state->offset;           // CT0 QTS
```

`ioc_submit_cl` (`v3d_phoenix_winsys.c:338`) already programs `CLE_CT0QBA/QEA`,
`CT0QMA/QMS`, `CT0QTS`, `CLE_CT1QBA/QEA`, services binner OOM via `PTB_BPOA/BPOS`, and
does the SLCACTL/L2T cache maintenance that the cube/Quake debugging proved essential.
**This means once V3DV's CLs are well-formed, the existing winsys executes them with
no change.** The extra V3DV-only fields — `bo_handles[]` (the kernel uses them to pin
BOs; our BOs are permanently resident in the flat MMU, so we ignore the list),
`flags` (`DRM_V3D_SUBMIT_CL_FLUSH_CACHE` — we always flush), and `extensions` (the
multi-sync chain) — are all harmlessly ignored by reading only the geometry fields.

### 1.2 Extra GET_PARAMs V3DV queries at init

`v3dv_device.c:1419` queries `DRM_V3D_PARAM_SUPPORTS_MULTISYNC_EXT` and (line 1422+)
`SUPPORTS_PERFMON`, and the CPU-queue cap. Add these to `ioc_get_param`:

- `SUPPORTS_MULTISYNC_EXT` → **return 1.** Counter-intuitive, but necessary: in this
  Mesa version `handle_cl_job` *unconditionally* sets `DRM_V3D_SUBMIT_EXTENSION` and
  zeroes the legacy `in_sync_bcl/in_sync_rcl/out_sync` fields (`v3dv_queue.c:903-909`).
  There is **no legacy single-sync code path left** to fall back to. So returning 0
  would not simplify anything; return 1 and let the winsys ignore the chained
  `extensions` pointer. (I verified there is no `if (caps.multisync)` branch around the
  submit in `v3dv_queue.c`.)
- `SUPPORTS_PERFMON` → return 0 (no perf queries).
- CPU-queue cap → return 0 if vkQuake issues no CPU jobs (it shouldn't); revisit only
  if device-create asserts on it.

### 1.3 TFU — the silent-drop correctness hole (must decide before Tier 5)

`ioc_get_param` advertises `SUPPORTS_TFU = 1` (`v3d_phoenix_winsys.c:417`), but
`phoenix_v3d_ioctl`'s switch has no `DRM_V3D_SUBMIT_TFU` case → it hits
`default: return 0` ("perfmon/tfu/csd: no-op for now"). **V3DV emits TFU jobs for
texture tiling/format conversion and some image copies/blits**
(`handle_tfu_job` at `v3dv_queue.c:933`, driven from `v3dv_meta_copy.c` /
`v3dvx_meta_common.c`). A no-op that returns *success* means the texture upload appears
to work but the destination BO is never written → **textures come back as garbage/zero
with no error anywhere.** Clears and a flat-shaded triangle do **not** use TFU, so
Tiers 2–3 are unaffected; this bites at texture time (Tier 5, and any textured Tier-4
test).

Two options, decide at Tier 5 design:

- **(A) cheapest first:** return `SUPPORTS_TFU = 0`. V3DV then performs the same
  operations as CL-based blits / shader copies instead of TFU. **Verify** that the
  meta-copy paths in `v3dv_meta_copy.c` actually honor `caps` and fall back (grep for
  the TFU capability check there before relying on this).
- **(B) if the CL fallback is too slow** (Quake uploads a lot of texture data once at
  map load, so this is likely *fine*): implement a real `ioc_submit_tfu` using the
  `drm_v3d_submit_tfu` struct (iia/ica/iis/icfg/...). Reference the register sequence in
  `external/linux/drivers/gpu/drm/v3d/v3d_sched.c:v3d_tfu_job_run` (programs the
  TFU registers in the HUB and waits on `V3D_HUB_INT_TFUC`). This is a self-contained
  add to the winsys mirroring how SUBMIT_CL was built from the scout.

### 1.4 CPU jobs and the job-type taxonomy

V3DV's `enum v3dv_job_type` (`v3dv_cmd_buffer.h:79`) is:
`GPU_CL`, `GPU_CL_INCOMPLETE`, `GPU_TFU`, `GPU_CSD`, and CPU jobs
`CPU_RESET_QUERIES`, `CPU_END_QUERY`, `CPU_COPY_QUERY_RESULTS`, `CPU_CSD_INDIRECT`,
`CPU_TIMESTAMP_QUERY`. The CPU jobs are dispatched through `DRM_V3D_SUBMIT_CPU` in
recent Mesa (`v3dv_queue.c:279,493,638,716`). Classic Quake/vkQuake uses **no occlusion
queries, no timestamps, no compute** → none of these fire. The winsys no-op is safe;
just confirm with a grep of the vkQuake source once obtained (§5).

### 1.5 Device enumeration — a hard Tier-1 gate GL never faced

`enumerate_devices` (`v3dv_device.c:1634`) calls `drmGetDevices2()` then
`open(devices[i]->nodes[DRM_NODE_RENDER])` for a `bustype == DRM_BUS_PLATFORM` device
named `"v3d"`. **Phoenix has no `/dev/dri/renderD*` node and no `drmGetDevices2`.** The
GL harness sidestepped this by handing gallium a synthetic fd directly. Plan: **patch
`enumerate_devices`** (in `mesa-phoenix-port.patch`) to skip the DRM scan and call
`create_physical_device(instance, /*primary*/-1, /*render*/FAKE_FD, /*display*/-1)`
directly. The fd value is inert: `drmIoctl` is the shim inline that forwards to
`phoenix_v3d_ioctl` regardless of fd. Also stub `drmGetVersion` (called at
`v3dv_device.c:1401`) to return a small static `drmVersion` with name `"v3d"`.

---

## 2. The synchronization gap — analyzed deeply (this is Q2/Q7, and it's GOOD news)

**Claim, stated firmly:** A Vulkan implementation that executes *all* GPU work
synchronously inside `vkQueueSubmit` and reports every fence/semaphore as
already-signaled is **conformant**. The spec mandates ordering and visibility
guarantees, not concurrency. "Most conservative legal scheduling" = do the work now,
signal everything. This is exactly the model the GL winsys already runs at 42fps@1080p:
`ioc_submit_cl` blocks on `FLDONE`/`FRDONE` before returning, and `v3d_libdrm_shim.c`
makes every syncobj op report signaled.

So for V3DV the sync objects carry **no real information** — by the time
`vkQueueSubmit` returns, the GPU is idle and all results are in RAM (the winsys does the
final L2T `FLM_CLEAN` flush, `v3d_phoenix_winsys.c:402`). Fences, binary semaphores, and
even timeline semaphores can all be backed by trivially-signaled stubs.

### 2.1 How V3DV reaches the sync layer, and the cleanest place to neutralize it

> **Layer verified (do not skip — this is the Tier-1 first-light gate).** I traced the
> full call chain to ground truth rather than asserting it. `v3dv_device.c:1480` sets
> `device->drm_syncobj_type = vk_drm_syncobj_get_type(render_fd)`. The runtime's
> `vk_drm_syncobj` (`src/vulkan/runtime/vk_drm_syncobj.c`) implements each `vk_sync` op
> as `device->sync->{create,signal,reset,wait,query,...}` — note these are **function
> pointers in a `util_sync_provider`, NOT direct libdrm calls** (the error strings say
> `DRM_IOCTL_SYNCOBJ_*`, which initially looks like raw ioctls). `vk_drm_syncobj_get_type`
> (`:708`) builds that provider via `util_sync_provider_drm(drm_fd)`, defined in
> **`src/util/u_sync_provider.c`**. I read it: its methods bottom out in the **libdrm
> `drmSyncobj*` C wrappers** — `drm_syncobj_create → drmSyncobjCreate` (`:25`),
> `→ drmSyncobjSignal/Reset/Query2/Wait/TimelineSignal/TimelineWait/Transfer`
> (`:62-103`). So `vk_drm_syncobj_init` → `device->sync->create` → **`drmSyncobjCreate`**.
>
> **Conclusion: the interception layer IS `v3d_libdrm_shim.c` (the `drmSyncobj*`
> wrappers), not the winsys's `phoenix_v3d_ioctl`.** Unlike the GEM_CLOSE core-DRM
> ioctl, the runtime never issues raw `DRM_IOCTL_SYNCOBJ_*` to `drmIoctl` — it calls the
> wrappers directly. Stubbing them in the shim is correct. (`vk_sync_dummy` would also
> work but needs V3DV source edits; rejected.)

In the submit path, `set_in_syncs`/`set_out_syncs` (`v3dv_queue.c:88,167`) extract
`vk_sync_as_drm_syncobj(sync)->syncobj` handles into the `drm_v3d_multi_sync` extension,
which the winsys then ignores.

**Action — complete the `drmSyncobj*` stub surface in `v3d_libdrm_shim.c`.** Zero V3DV
source edits. The current shim has `drmSyncobjCreate`/`Destroy`/`Wait`/`ImportSyncFile`/
`ExportSyncFile`. The provider in `u_sync_provider.c` *also* references — and so the shim
**must additionally define** — the exact set: `drmSyncobjSignal`, `drmSyncobjReset`,
**`drmSyncobjQuery2`** (note the `2` suffix — that's what the provider calls, `:96`),
`drmSyncobjTimelineSignal`, `drmSyncobjTimelineWait`, `drmSyncobjTransfer`,
`drmSyncobjHandleToFD`, `drmSyncobjFDToHandle`. All trivially-signaled: Signal/Reset →
return 0; Query2 → write a monotonic "already passed" point value; TimelineWait/Wait →
return 0 immediately (work already done); HandleToFD/FDToHandle → -1 (no sharing). The
`util_sync_provider`'s `finalize`/`get_drm` (`:112-115`) also resolve through libdrm — a
missing one is a link error, so add whatever the link-drive reports. **Verify the precise
wrapper list against `u_sync_provider.c`'s vtable initializer (`:126` onward) when the
link-drive surfaces undefined symbols — that file is the authoritative list.**

Either way the **semantics are irrelevant** because work is done before the sync object
is ever queried. Choose the stub-completion path: it touches only `v3d_libdrm_shim.c`.

### 2.2 The `vk_queue` submit thread — harmless

`vk_queue` can spin a background submit thread when certain sync features are advertised.
On Phoenix this is harmless: the thread would just block inside our synchronous
`ioc_submit_cl` and signal the (already-trivial) syncs. It does, however, mean the
**C11 threads shim must work** (`thrd_create`, `mtx_*`, `cnd_*`), because `vk_queue`,
`vk_device`, and `util/u_thread` use them. This is part of the Tier-0 closure (§4.3).
If the thread is problematic, set the device to the no-submit-thread mode (single-thread
queue) — verify the flag in `vk_queue_init`.

### 2.3 What we are giving up (the honest cost)

Single frame in flight. No CPU/GPU overlap, no pipelined acquire→render→present. That
caps fps below Linux's async 60. The GL path runs the same way and is playable, so this
is a *performance* ceiling, not a correctness wall. If fps proves unacceptable later, the
*minimal* async facility to add would be: a V3D end-of-frame IRQ handler (the FRDONE
interrupt we currently poll) wired to a Phoenix condvar, letting `vkQueueSubmit` return
before FRDONE and a real fence wait block on the condvar. That is a substantial winsys
change (real IRQ registration on the V3D IRQ line, an in-flight-job tracker) and should
be explicitly **out of scope for first-light**; list it as a post-Tier-5 perf option.

---

## 3. WSI / swapchain — bypass the standard WSI, build a minimal fb0 swapchain

`v3dv_wsi.c` uses `wsi_common_drm` + a **separate display DRM node** + PRIME to share
buffers between render and display devices. On Phoenix all of that is dead:
`v3dv_wsi_can_present_on_device` *asserts* `display_fd != -1` (`v3dv_wsi.c:43`), there is
no display node, and `drmPrime*` is stubbed to fail. **Do not initialize the standard
WSI.**

**Plan: a minimal in-ICD swapchain that presents to `/dev/fb0`.** We already have the
hard part — the GL path's *render-to-scanout* (`ioc_create_bo` backs a `SCANOUT`-flagged
BO with the framebuffer's physical pages, `v3d_phoenix_winsys.c:268`, and
`v3d_phoenix_set_scanout` / `v3d_phoenix_scanout_active` plumb the fb0 PA in). The
swapchain reuses this:

- `vkCreateSwapchainKHR`: query `/dev/fb0` mode via `RPI4FB_GETMODE`, call
  `v3d_phoenix_set_scanout(fb_pa, pitch*height)`, allocate **N=2** images. Image 0's
  backing BO is the scanout BO (GPU renders straight to screen). For double-buffering,
  give image 1 an offscreen BO and `vkQueuePresentKHR` copies it to fb0 (CPU memcpy or a
  CL blit) — or, simpler for first-light, **N=1 single-buffered straight to scanout**
  (tearing, but Quake at our fps won't care, and the GL path does exactly this).
- `vkAcquireNextImageKHR`: return the next index immediately (sync is trivial).
- `vkQueuePresentKHR`: if rendering to scanout, nothing to do (already on screen,
  matching `v3dv_scanout_active`); else blit/copy to fb0. Continuous re-blit at ~3Hz to
  beat the fbcon klog mirror, exactly as the GL capstone documented.

Implementation: provide our own `v3dv_CreateSwapchainKHR` /
`AcquireNextImageKHR` / `QueuePresentKHR` / `GetSwapchainImagesKHR` /
`CreateXxxSurfaceKHR` entrypoints (override the WSI-provided ones via the generated
dispatch), and either skip `wsi_device_init` or feed it a dummy. The `VK_KHR_surface` +
`VK_KHR_swapchain` extensions must still be *advertised* (vkQuake requires them) but
backed by our code.

**Honest risk (sleeper):** vkQuake is coupled to swapchain acquire/recreate/present
semantics (out-of-date/suboptimal handling, `VK_ERROR_OUT_OF_DATE_KHR` on resize) more
tightly than quakespasm was to the GL `vid` layer (`pl_phoenix_vid.c` was a thin shim).
We have a fixed-mode framebuffer (no resize), which *helps* — always return
`VK_SUCCESS`, never out-of-date — but the vkQuake vid rewrite (`vid_vulkan.c`
equivalent) is a bigger lift than the GL one. Rate it Hard.

---

## 4. Build plan — compiling V3DV for aarch64-phoenix

Model it on `build-v3d-phoenix.py`: a host Mesa build supplies per-file compile flags
via `compile_commands.json`; we re-emit each compile with the Phoenix toolchain, force-
include `phoenix_mesa_compat.h`, prepend `shim-include/`, then **link-drive** to
discover the real symbol closure (undef → add the defining .c → re-run, converges in a
few passes). This already works for ~hundreds of objects.

### 4.1 Host build must include V3DV

The current `/tmp/mesa-v3d-build` is configured for gallium only. Reconfigure the host
Mesa with `-Dvulkan-drivers=broadcom` so `compile_commands.json` gains entries for
`src/broadcom/vulkan/*.c`, `src/vulkan/runtime/*.c`, `src/vulkan/util/*.c`, and the
generated files (`v3dv_entrypoints.[ch]`, `vk_common_entrypoints`, the format/dispatch
tables). The build script's `driver_entries()` filter (`/gallium/drivers/v3d/`) must be
extended (or a parallel `v3dv_entries()` added) to pull `/broadcom/vulkan/` and the
runtime dirs.

### 4.2 Massive reuse — the broadcom back-end is already cross-built

`v3dv_deps` in `src/broadcom/vulkan/meson.build` lists `dep_v3d_hw`,
`idep_broadcom_perfcntrs`, `idep_nir`. These are the **same** libraries the gallium
build links. The CLE packers, `v3d_compile` (NIR→QPU), the QPU encoder, perfcntrs, and
the V3D simulator stubs are *already* in `/tmp/libv3d-phoenix.a`. V3DV adds front-end
objects on top of an already-proven back-end. Concretely, the per-version files
`v3dvx_*.c` (built for `V3D_VERSION` 42 and 71 — we only need **42**) sit alongside the
gallium `v3dx_*.c` we already compile per-version; reuse the exact same
`-DV3D_VERSION=42` selection logic already in `build-v3d-phoenix.py`.

### 4.3 New closure to expect (the bigger-than-GL part)

- **`src/vulkan/runtime/`** — `vk_device`, `vk_queue`, `vk_command_buffer`,
  `vk_descriptors`, `vk_pipeline`, `vk_sync*`, `vk_drm_syncobj`, `vk_object`,
  `vk_instance`, plus `src/vulkan/util/` (`vk_enum_to_str`, format tables). This is the
  Vulkan equivalent of the GL `st/mesa` frontend closure and is **larger** than the GL
  link.
- **A C11 `<threads.h>` shim** — the runtime uses `mtx_t`/`cnd_t`/`thrd_t`. libphoenix
  has `sys/threads.h` (mutex/cond/beginthread) but not C11 `threads.h`. Add a
  `shim-include/threads.h` mapping `mtx_*`→Phoenix mutex, `cnd_*`→Phoenix cond,
  `thrd_create`→`beginthread`. This is the one genuinely new shim vs. the GL port.
- **`spirv_to_nir`** (`src/compiler/spirv/`) — vkQuake ships **SPIR-V** shaders
  (GL used the GLSL frontend, which we built; Vulkan does not). `spirv_to_nir` is
  CPU-only NIR generation, no new HW interaction; just more objects in the closure.
- **Generated dispatch** — reuse the **host-generated** `v3dv_entrypoints.c`,
  `vk_common_entrypoints.c`, format tables (don't try to run the python generators in
  the Phoenix build; copy the host outputs, exactly as the GL build reuses host-
  generated headers).
- The libdrm surface (`drmGetDevices2`, `drmGetVersion`, `drmFreeDevices`) — add to
  `v3d_libdrm_shim.c` (§1.5).

### 4.4 Loader vs. ICD — link the ICD directly, skip the loader

vkQuake calls Vulkan via `vkGetInstanceProcAddr`. It does **not** need the full Khronos
loader (`libvulkan.so` / ICD JSON discovery / layers). **Statically link the V3DV ICD**
and resolve entrypoints directly: provide `vkGetInstanceProcAddr` →
`v3dv_GetInstanceProcAddr` (the generated dispatch). If vkQuake hard-links
`vkCreateInstance` etc. as symbols, alias them to the `v3dv_*` entrypoints (a thin
`vk_icd_link.c`, same spirit as `gl_stubs.c`). No loader, no JSON, no `dlopen`. This
mirrors how the GL build linked `libGL-phoenix.a` directly without a GLX/EGL loader.

---

## 5. vkQuake porting

vkQuake (Axel Gneiting's port of QuakeSpasm to Vulkan) is **not in the repo yet** —
obtain it (`github.com/Novum/vkQuake`) into `external/vkquake/` (offline clone, like
`external/quakespasm`). It is *derived from* QuakeSpasm and shares the non-renderer
code (game logic, file I/O, net, sound, input), so the existing
`tools/quakespasm-port/` shims are **directly reusable**:

- `platform/pl_phoenix_sys.c` (Sys_*), `pl_phoenix_main.c`, `pl_phoenix_in.c`
  (the `/dev/kbd0` decoder), `pl_phoenix_snd.c`, `pl_phoenix_stubs.c` (loopback net
  driver) — all carry over with little/no change.
- `sdl-shim/SDL.h` + the SDL shim — vkQuake uses SDL2 for window/input/timer. The
  existing shim covered the GL subset; **it must grow the Vulkan surface bits**:
  `SDL_Vulkan_CreateSurface`, `SDL_Vulkan_GetInstanceExtensions`,
  `SDL_Vulkan_GetDrawableSize`. Point `SDL_Vulkan_CreateSurface` at our custom
  `vkCreate*SurfaceKHR` (a fb0 surface, §3), and have `GetInstanceExtensions` return
  `VK_KHR_surface` (+ our platform surface ext name).
- **The renderer is what's new** — vkQuake's `Vulkan/` directory (vid_vulkan.c,
  gl_*.c-equivalents rewritten for Vulkan, the `.spv` shaders, descriptor sets,
  pipelines). This is the bulk of the new porting work and depends entirely on Tiers
  1–4 being solid first.

vkQuake targets **Vulkan 1.x core + `VK_KHR_swapchain`**; verify the exact required
version/extensions from its `vkCreateInstance` (likely 1.0 or 1.1 + swapchain + maybe
`VK_KHR_get_physical_device_properties2`). V3DV advertises far more than vkQuake needs.
**Grep the obtained vkQuake source** to confirm it issues no occlusion queries /
timestamps / compute (validates the §1.4 CPU-job no-op assumption) and to enumerate the
exact swapchain/present call sequence (validates §3).

Effort vs. quakespasm: the **non-renderer** shim reuse is ~free; the **renderer +
swapchain coupling** is a fresh, large piece, comparable to (and somewhat larger than)
the original GL renderer bring-up, *plus* the entire V3DV stack underneath it that GL
didn't need.

---

## 6. Phased, commit-stoppable decomposition

Each tier ends at a committable, verifiable state, mirroring the GL port's tier
discipline. "Proof" = a UART/log line or a screenshot the rebuild→capture→summarize loop
can confirm.

### Tier 0 — V3DV links for Phoenix
**Obstacle:** get `src/vulkan/runtime` + `spirv_to_nir` + V3DV front-end through the
cross-compiler and link-drive to a clean closure under libphoenix; the C11-threads shim.
**Proof:** `build-v3dv-phoenix.py` link-drive reaches 0 undefined symbols; a
`vkGetInstanceProcAddr` smoke harness links (analog of `gl_frontend_smoke.c`).
**Difficulty: Medium-High.** Mostly mechanical (the GL port proved the recipe), but
larger closure + the threads shim are new. **Risk: Medium** (libphoenix gaps surface as
link errors, one at a time — tractable, just slow).

### Tier 1 — vkCreateInstance + vkCreateDevice succeed on real HW
**Obstacle:** device enumeration bypass (§1.5), the new GET_PARAMs (§1.2), the completed
`drmSyncobj*` surface (§2.1), `drmGetVersion`. V3DV must power on the V3D
(`winsys_init` already calls `v3d_phoenix_powerOn`) and pass `v3d_get_device_info`
(needs the real IDENTs — already present).
**Proof:** boot-launched harness prints `vkCreateDevice OK, V3D 4.2` (V3DV will read the
IDENTs through GET_PARAM, same as the GL `v3d_screen_create` did).
**Difficulty: Medium.** **Risk: Medium** — enumeration patch + sync-vtable completeness
are the gotchas; both are localized.

### Tier 2 — vkCmdClear to an image + glReadPixels-equivalent readback
**Obstacle:** the full command-buffer record→submit→sync path end to end: descriptor
pool, render pass / dynamic rendering, `v3dv_meta_clear` emits a CL, the CL goes through
SUBMIT_CL (reused winsys), readback from a CACHEABLE BO with `dc ivac`.
**Proof:** clear an image to a known color, read center pixel == that color (the
direct analog of the GL clear milestone, MEMORY `25v`).
**Difficulty: Medium.** **Risk: Medium-Low** — clears don't use TFU; this exercises the
proven SUBMIT_CL path with V3DV-built CLs. First real "the stack runs" milestone.
**Coherency pre-warning (same silent-corruption class as the TFU trap):**
`v3dv_GetPhysicalDeviceMemoryProperties` advertises memory types with
`HOST_COHERENT` / `HOST_CACHED` flags. The executor must map these onto the winsys BO
modes — `HOST_COHERENT` → uncached-contiguous (`ioc_create_bo` default), `HOST_CACHED`
→ the cacheable path (`flags & V3D_CREATE_BO_CACHEABLE`, `v3d_phoenix_winsys.c:296`) with
explicit `dc ivac` before CPU reads. Caches are ON (TD-16 resolved), so advertising
`HOST_COHERENT` while backing it with cached-without-auto-flush memory (or vice-versa)
produces silent readback corruption with no fault. Pin the advertised memory-type flags
to what the winsys actually delivers (likely: advertise a single uncached HOST_COHERENT
type for first-light), and revisit only if a cached upload path is needed for perf.

### Tier 3 — a triangle
**Obstacle:** a graphics pipeline: `spirv_to_nir` on a trivial SPIR-V VS+FS,
`v3dv_pipeline` → `v3d_compile` (reused) → QPU, vertex BO, draw, RCL store.
**Proof:** readback shows the triangle's color where expected (analog of the GL
textured-triangle milestone, MEMORY `25w`).
**Difficulty: Medium-High.** **Risk: Medium** — pipeline/descriptor machinery is the
most code between us and pixels, but the *back-end* (compile + submit) is proven.

### Tier 4 — present a triangle to /dev/fb0
**Obstacle:** the custom fb0 swapchain (§3) wired to `v3d_phoenix_set_scanout` +
render-to-scanout (proven in GL) or copy-to-fb0; continuous re-blit.
**Proof:** triangle visible on the HDMI screenshot (`artifacts/hdmi/`), analog of the GL
on-HDMI milestones.
**Difficulty: Medium.** **Risk: Low-Medium** — the scanout mechanism is already
HW-proven; risk is purely in the swapchain entrypoint wiring.

### Tier 5 — vkQuake
**Obstacle:** everything in §5 — obtain/port vkQuake, grow the SDL Vulkan shim, the
TFU decision (§1.3) for textures, the renderer + swapchain coupling, reuse the
quakespasm shims, input via `/dev/kbd0` (already decoded, blocked on EBUSY per the GL
capstone — same unblock applies).
**Proof:** vkQuake renders a textured level on HDMI, demos cycle, 0 faults — the
vkQuake analog of the GL textured-world milestone.
**Difficulty: High.** **Risk: High** — TFU/texture correctness (silent-drop trap),
swapchain coupling, and the sheer size of vkQuake's Vulkan renderer. This is the long
pole.

---

## 7. Honest assessment

**Timeline: months, not weeks.** Tiers 0–2 (links, instance/device, clear) are the
"reuse the GL recipe + complete the kernel-ABI surface" phase and are the
fastest/most-predictable — plausibly a few focused weeks given the GL port's tooling.
Tier 3 (pipeline/triangle) is a meaningful step up. Tiers 4–5 (swapchain + the entire
vkQuake Vulkan renderer) dominate the schedule and carry the real uncertainty.

**The 2–3 things most likely to kill it (in order):**

1. **The runtime + SPIR-V closure under libphoenix (Tier 0).** Not architecturally
   risky, but the largest unknown labor: every missing libc/threads/atomic primitive is
   a link error to resolve one at a time. The GL port shows this *converges*, but the
   Vulkan runtime closure is bigger and adds C11 threads. If libphoenix has a hard gap
   (e.g. something the `vk_queue` submit thread or `util/u_thread` needs that can't be
   shimmed), this could stall.
2. **Copy/meta correctness via the TFU story (Tier 5).** The `SUPPORTS_TFU=1` +
   silent-drop trap (§1.3) produces *silent* texture corruption with no fault, the
   worst kind to debug. Must be resolved deliberately (return 0 and verify the CL
   fallback, or implement real TFU) before textured content.
3. **vkQuake's swapchain/present coupling (Tier 4–5).** More entangled than the GL vid
   shim; our fixed-mode fb0 swapchain has to satisfy vkQuake's acquire/present/recreate
   expectations. Fixed resolution *helps* (never out-of-date), but the renderer rewrite
   is large.

**Is the synchronous-winsys model viable for Vulkan at all? Yes — unequivocally.**
Synchronous, single-in-flight submit with pre-signaled sync objects is conformant; the
GL path proves the identical model on the identical HW at playable speed. Vulkan needs
async submission only to raise the fps ceiling, not to be correct. The minimal async
facility — *if* fps ever demands it — is a V3D FRDONE-IRQ → condvar in the winsys so
`vkQueueSubmit` can return before render completion; this is explicitly a **post-Tier-5
performance option**, not a prerequisite. First-light vkQuake should ship synchronous.

**Bottom line:** The architecture is sound and the back-end is already proven and built.
This is a labor-and-debugging project, not a research gamble. Execute it tier by tier;
do not skip the TFU decision before texturing; build the custom fb0 swapchain rather
than fighting `wsi_common_drm`.

---

## Appendix A — concrete first-week checklist (Tier 0/1 kickoff)

1. Reconfigure host Mesa: `meson configure /tmp/mesa-v3d-build -Dvulkan-drivers=broadcom`
   (or a fresh build dir); confirm `compile_commands.json` gains
   `src/broadcom/vulkan/*` and `src/vulkan/runtime/*` entries.
2. Fork `build-v3d-phoenix.py` → `build-v3dv-phoenix.py`: extend the entry filter to
   include `/broadcom/vulkan/` (V3D_VERSION=42 only), `/vulkan/runtime/`,
   `/vulkan/util/`, `/compiler/spirv/`; reuse the host-generated `v3dv_entrypoints.c` etc.
3. Add `shim-include/threads.h` (C11 → `sys/threads.h`).
4. Extend `v3d_libdrm_shim.c`: `drmGetDevices2`/`drmFreeDevices`/`drmGetVersion` +
   `drmSyncobjSignal`/`Reset`/`Query` (+ timeline variants if kept).
5. Add to `ioc_get_param`: `SUPPORTS_MULTISYNC_EXT=1`, `SUPPORTS_PERFMON=0`,
   CPU-queue cap as needed.
6. Patch `enumerate_devices` (in `mesa-phoenix-port.patch`) to bypass `drmGetDevices2`
   and call `create_physical_device(instance, -1, FAKE_FD, -1)`.
7. Link-drive to 0 undefined; write a `vkCreateInstance`/`vkCreateDevice` smoke harness;
   boot it and confirm `V3D 4.2` device-info readback.

## Appendix B — key source coordinates (for the executor)

- Winsys / fake kernel: `tools/v3d-driver-port/v3d_phoenix_winsys.c`
  (`ioc_submit_cl:338`, `ioc_create_bo:239`, `ioc_get_param:407`, `phoenix_v3d_ioctl:433`).
- libdrm surface: `tools/v3d-driver-port/v3d_libdrm_shim.c`.
- Build recipe: `tools/v3d-driver-port/build-v3d-phoenix.py`.
- V3DV submit: `external/mesa/src/broadcom/vulkan/v3dv_queue.c`
  (`handle_cl_job:797`, `set_multisync:218`, `set_in_syncs:88`, `handle_tfu_job:933`).
- V3DV device/enum: `external/mesa/src/broadcom/vulkan/v3dv_device.c`
  (`enumerate_devices:1634`, sync_types `:1480-1494`, GET_PARAM caps `:1419`).
- V3DV WSI: `external/mesa/src/broadcom/vulkan/v3dv_wsi.c`.
- Job types: `external/mesa/src/broadcom/vulkan/v3dv_cmd_buffer.h:79`.
- Vulkan runtime sync: `external/mesa/src/vulkan/runtime/vk_drm_syncobj.c`,
  `vk_sync_dummy.c`, `vk_sync_timeline.c`, `vk_sync_binary.c`.
- UAPI structs: `tools/v3d-driver-port/v3d_drm.h` (multisync `:114`, submit_cl,
  submit_tfu `:54`, submit_csd `:353`, submit_cpu `:579`).
- Kernel reference (TFU/IRQ/sched): `external/linux/drivers/gpu/drm/v3d/v3d_sched.c`,
  `v3d_irq.c`.
- GL precedent docs: `docs/inprogress/2026-06-11-v3d-driver-port-architecture.md`,
  `docs/inprogress/2026-06-15-glquake-capstone-status.md`.
