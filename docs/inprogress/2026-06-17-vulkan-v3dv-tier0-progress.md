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

## Mesa source edits (COMMITTED in external/mesa @ dbd03bef831, all `#if __phoenix__`-guarded)

- `src/vulkan/runtime/vk_image.{h,c}` — extend the four
  `#if DETECT_OS_LINUX || DETECT_OS_BSD` guards (the `drm_format_mod` field decl + its
  3 uses) with `|| defined(__phoenix__)`, so the modifier field exists (v3dv_image.c
  accesses it unconditionally). **Vulkan-runtime-only files** — the gallium GL build
  never compiles them, so the main agent's GLQuake stack is untouched.
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

**GL-build impact of the MULTISYNC 0→1 flip (shared-lib runtime change — VERIFIED, not
assumed).** `v3d_phoenix_winsys.c` is baked into the shared `libv3d-phoenix.a` the main
agent's GLQuake links, and gallium's `v3d_screen.c:829-831` DOES read all three caps into
`screen->{has_perfmon,has_cpu_queue,has_multisync}` — so the flip sets GL's
`has_multisync` false→true too. Checked every gallium use:
- **submit hot path** (`v3d_job.c`, `v3dx_draw.c`) uses the LEGACY in_sync_bcl/
  in_sync_rcl/out_sync fields directly, does NOT branch on has_multisync → the GLQuake
  per-frame submit path is UNCHANGED.
- `query_time_elapsed = has_cpu_queue && has_multisync` (v3d_screen.c:273) stays 0
  (CPU_QUEUE=0) → advertised caps unchanged.
- only delta: `assert(screen->has_multisync)` (v3d_query.c:258) in a timestamp-query
  path classic Quake doesn't exercise — and it makes the assert PASS, never fire.
Net: GL-runtime-inert in practice. libv3d-phoenix.a was rebuilt to bake in this edit;
the GL harness still links PASS.

## Why per-site `__phoenix__` guards, NOT a global detect_os.h masquerade

The first attempt set `__phoenix__ ⇒ DETECT_OS_LINUX/POSIX` globally in `detect_os.h`.
That **regressed the shared core build**: it flipped `src/util/u_cpu_detect.c` onto its
`#if DETECT_OS_LINUX → #include <sys/auxv.h>` / `getauxval` path, which the Phoenix
toolchain lacks → u_cpu_detect.c failed → `libv3d-phoenix.a` lost
`u_init_pipe_screen_caps` → the V3DV link (and, critically, the main agent's GLQuake
gallium build, which shares detect_os.h) broke. `detect_os.h` is global; flipping it
would silently recompile the entire gallium driver under DETECT_OS_LINUX=1 (mmap/anon-
file/thread paths) on the main agent's next rebuild — a silent regression that can't be
boot-verified unattended.

Resolution: revert the global change; add `|| defined(__phoenix__)` only to the four
`#if DETECT_OS_LINUX || DETECT_OS_BSD` sites in `vk_image.{h,c}` (Vulkan-runtime-only,
never compiled by the GL build). Verified: the GL build is fully restored (build-v3d-
phoenix.py → `[link] PASS`, 0 FAIL, `u_init_pipe_screen_caps` defined), and a V3DV
`--compile-only` surfaced **no further** per-site DETECT_OS needs (the closure has none
beyond vk_image). `os_misc.c` (the `/proc`-ish memory probes) is deliberately NOT pulled
— it collides with the GL port's os_* stubs, and the one symbol needed is stubbed.

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
1b. **Harness NULL-instance procaddr quirk.** The harness fetches `vkCreateInstance`
   via `vkGetInstanceProcAddr(NULL, ...)`. NULL-instance procaddr is spec-valid only for
   a few global entrypoints (CreateInstance / EnumerateInstance*); if the runtime's
   GetInstanceProcAddr doesn't special-case them it returns NULL at runtime (harness
   prints `vkGetInstanceProcAddr(vkCreateInstance) NULL`). That is a harness/loader
   quirk, NOT a V3DV bug — if it bites, fetch CreateInstance directly as
   `v3dv_CreateInstance` or check vk_common's global-procaddr handling.
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
