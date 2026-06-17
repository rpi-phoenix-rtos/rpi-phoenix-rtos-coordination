# Vulkan V3DV Tier 0 — link closure for Phoenix-RTOS RPi4 (progress)

**Date:** 2026-06-17
**Status:** Tier 0 COMPLETE (compile + link, statically self-verified). Boot-verification = Tier 1 (main agent).
**Plan:** docs/inprogress/2026-06-16-vulkan-v3dv-vkquake-port-plan.md (Tier 0 + §4 build plan).
**Mirrors:** the GL gallium port (tools/v3d-driver-port/build-v3d-phoenix.py / build-gl-phoenix.py).

## TL;DR

Mesa's V3DV (Vulkan for Broadcom V3D) driver + the Vulkan runtime + `spirv_to_nir`
now **compile and link for aarch64-phoenix**. A `vkCreateInstance →
vkEnumeratePhysicalDevices → vkCreateDevice` harness links to a statically-linked
aarch64 ELF with **0 undefined symbols** (`nm -u` clean, not just a passing link).
The broadcom back-end (`v3d_compile`/CLE/QPU/perfcntrs) is reused **as-is** from
`/tmp/libv3d-phoenix.a` (the GL port already cross-built it). The plan's predicted
C11-threads.h shim was **not needed** — `HAVE_PTHREAD` already routes Mesa's
`c11/threads.h` to the pthread path, and `cnd_*/thrd_*` bodies come from
`src/c11/impl/threads_posix.c` (same TU the GL build pulled).

## What compiles / links

| Piece | Status |
|---|---|
| `src/broadcom/vulkan/*.c` (V3D_VERSION=42, **`v3dv_wsi.c` excluded**) | 96 TUs OK |
| generated `v3dv_entrypoints.c` | OK |
| `src/vulkan/runtime/*.c` + `src/vulkan/util/*.c` (incl. generated dispatch/enum tables) | OK (in the 96) |
| `src/compiler/spirv/*` (spirv_to_nir + vtn_*) | OK (aux list) |
| `src/util/u_sync_provider.c` (drmSyncobj* vtable), `src/c11/impl/threads_posix.c` | OK (aux list) |
| harness + shims | OK |
| **final link** | **rc=0, `nm -u` = 0 undefined, ELF = ARM aarch64 static** |

## Build

`tools/v3d-driver-port/build-v3dv-phoenix.py` (forked from build-v3d-phoenix.py).

- **Separate Vulkan host build dir** `/tmp/mesa-v3dv-build` (configured
  `-Dvulkan-drivers=broadcom`); the GL build's `/tmp/mesa-v3d-build` is untouched.
  Host meson needs a python with `mako`+`pyyaml` — the system python3.14 had mako
  uninstalled, so a `uv` venv (`/tmp/mesa-pyenv`) supplies meson/ninja/mako/pyyaml.
  Exact commands are in the build-script header.
- Front-end bulk-compiled; spirv + sync-provider + threads via the link-drive **aux
  list** (`tools/v3d-driver-port/v3dv-aux-sources.txt`, committed → reproducible).
- Reuses `/tmp/libv3d-phoenix.a` for the back-end; link group is
  `--start-group libv3dv-phoenix.a libv3d-phoenix.a --end-group`.

## New files (in tools/v3d-driver-port/, for the main agent to commit in coord repo)

- `build-v3dv-phoenix.py` — the cross-build + link-drive recipe.
- `v3dv_harness.c` — Tier-0 link harness (vkCreateInstance/EnumeratePhysicalDevices/
  CreateDevice via vkGetInstanceProcAddr).
- `vk_icd_link.c` — loader-less ICD linkage: `vkGetInstanceProcAddr →
  v3dv_GetInstanceProcAddr`; WSI bypass stubs (zeroed `wsi_*_entrypoints` tables,
  no-op `v3dv_wsi_init/finish`, trapping `wsi_common_*`).
- `v3dv_libdrm_shim.c` — the drmSyncobj* completion surface beyond the GL subset
  (`Signal/Reset/Query2/TimelineSignal/TimelineWait/Transfer/HandleToFD/FDToHandle`),
  plus `drmGetVersion/drmFreeVersion`, `drmGetDevices2/drmFreeDevices`, and the KMS
  display-probe stubs (`drmModeGetResources/...` → NULL). All synchronous-submit
  no-ops: by the time any sync object is queried the winsys has already blocked on
  FLDONE/FRDONE, so everything reports "signaled / passed".
- `v3dv_v71_stubs.c` — **57 weak TRAP stubs** for the dead V3D-7.1 dispatch branch.
  `v3d_X(devinfo, fn)` (src/broadcom/common/v3d_util.h) takes the address of BOTH
  `&v3d42_fn` and `&v3d71_fn`, so both symbol sets must link even though `case 71`
  is unreachable on V3D-4.2 (Pi4). We build v3dvx_* at V3D_VERSION=42 only; the v71
  symbols are weak `abort()` traps (fail loud, never silently return garbage). A real
  `-DV3D_VERSION=71` backend (future Pi5) overrides them with no edit. Generated from
  the link-drive undef list.
- `v3dv_gap_stubs.c` — libc/libdrm gaps: `secure_getenv`/`os_get_option_secure`
  (→getenv), `drmIsKMS` (→0), `c23_timespec_get`/`clock_getres` (→clock_gettime),
  `build_id_length`/`copy_build_id_to_sha1` (fixed 20-byte id), driconf
  `driParseOptionInfo`/`driDestroy*` (no-op = defaults).
- `v3dv-aux-sources.txt` — committed link-drive manifest.
- Edited shims: `shim-include/xf86drm.h` (drmSyncobj*/drmVersion/drmDevice/DRM_CAP
  decls, guarded WAIT_FLAGS), `shim-include/xf86drmMode.h` (KMS structs),
  `shim-include/dlfcn.h` (RTLD_NOLOAD etc.), `phoenix_mesa_compat.h` (_SC_PHYS_PAGES).

## Mesa source edits (COMMITTED in external/mesa @ 7b12e80eee0, all `#if __phoenix__`-guarded)

- `src/util/detect_os.h` — `__phoenix__` ⇒ DETECT_OS_LINUX/POSIX (so the
  LINUX||BSD-gated `vk_image.drm_format_mod`, accessed unconditionally in
  v3dv_image.c, exists).
- `include/renderdoc_app.h` — `__phoenix__`/`__unix__` ⇒ RENDERDOC_CC (was
  `#error "Unknown platform"`).
- `src/broadcom/vulkan/v3dv_device.c` — `enumerate_devices` bypasses the
  `drmGetDevices2` scan and calls `create_physical_device(instance, -1,
  V3DV_PHOENIX_FAKE_RENDER_FD, -1)`; `create_physical_device` skips the `fstat()` of
  the absent DRM nodes.

## Winsys cap additions (tools/v3d-driver-port/v3d_phoenix_winsys.c, ioc_get_param)

Added for Tier-1 device-create: `SUPPORTS_MULTISYNC_EXT=1` (REQUIRED — this Mesa's
`handle_cl_job` unconditionally sets DRM_V3D_SUBMIT_EXTENSION and zeroes the legacy
single-sync fields; there is no legacy path, and the winsys ignores the chained
extensions pointer because submit is synchronous), `SUPPORTS_PERFMON=0`,
`SUPPORTS_CPU_QUEUE=0`.

## DETECT_OS_LINUX blast radius (boot-verify on HW)

Masquerading as Linux flips compile-time paths. Within the **actual closure** (the
runtime + util + broadcom TUs we build), the ONLY `DETECT_OS_LINUX || DETECT_OS_BSD`
sites are in `vk_image.h` / `vk_image.c`, and they are pure struct-field /
modifier-tiling logic (init `drm_format_mod` to `DRM_FORMAT_MOD_INVALID`), **no
syscalls / no filesystem**. No `memfd_create` / `/proc/self` paths are in the
closure. So the LINUX shim is low-risk here. (`os_misc.c` — which has the
`/proc`-ish memory probes — is deliberately NOT pulled; it collides with the GL
port's os_* stubs, and the one symbol needed is stubbed.) A dedicated
`DETECT_OS_PHOENIX` is the cleaner long-term upstream refactor.

## Remaining undefined symbols / unbuilt TUs

**None.** `nm -u /tmp/v3dvphx-harness` = 0. The link is a real closure (verified with
`nm -u`, not just the linker's "undefined reference" parse, since `--gc-sections`
could otherwise drop a ref a runtime path needs).

One tolerated link relaxation: `-Wl,--allow-multiple-definition`, used SOLELY for 3
per-version helpers that V3DV (`v3dvx_{cmd_buffer,formats}.c`) and the gallium
back-end (`gallium/drivers/v3d/v3dx_{job,format_table}.c`) each define their own copy
of (`v3d42_job_emit_enable_double_buffer`,
`v3d42_get_internal_type_bpp_for_output_format`, `v3d42_tfu_supports_tex_format`).
Upstream these two drivers are never in one link; ours links them together. Link
order (libv3dv FIRST) binds V3DV's copy, which is correct. **CAVEAT:** any NEW
multiple-definition during Tier 1-5 must be investigated, not absorbed.

## Tier-1 next steps (main agent, boot-verify on HW)

1. **Boot the harness.** Build a boot-launched `rpi4-v3dv-tier0` from
   `v3dv_harness.c` + `libv3dv-phoenix.a` + `libv3d-phoenix.a` (self-power the V3D via
   `v3d_phoenix_power.c`, like rpi4-glclear). Expect the printf trail
   `vkCreateInstance -> 0` / `vkEnumeratePhysicalDevices -> 0 count=1` / `device
   name='...'` / `vkCreateDevice -> 0` / `PASS`.
2. **build_id (likely first failure).** `init_uuids` (v3dv_device.c:842) calls
   `build_id_find_nhdr_for_addr(init_uuids)` — defined in libv3d-phoenix.a, does a
   real ELF program-header walk, and may return **NULL** on the Phoenix ELF, which
   hard-fails device-create BEFORE the stubbed `build_id_length` is reached. If
   vkCreateDevice fails here, add a weak `build_id_find_nhdr_for_addr` to
   `v3dv_gap_stubs.c` returning a fixed synthetic note (the `build_id_length`/
   `copy_build_id_to_sha1` stubs already assume 20 fixed bytes).
3. **GET_PARAM caps** are wired (above) — confirm `v3d_get_device_info` reports ver
   42 through the real IDENTs (same path the GL `v3d_screen_create` used).
4. **driconf no-op** — confirm no required option is consulted via `driQueryOption*`
   on the empty cache (else port xmlconfig or stub the query side).
5. **Then Tier 2** (vkCmdClear + readback): coherency — pin the advertised
   `VkMemoryType` flags to what the winsys actually delivers (advertise a single
   uncached HOST_COHERENT type for first-light; see plan §Tier 2 coherency warning).
