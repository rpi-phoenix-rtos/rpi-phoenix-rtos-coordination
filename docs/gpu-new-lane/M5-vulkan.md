# M5 part 1 — Vulkan (v3dv) on the DRM-shaped stack + `VK_KHR_display` (vkcube)

Milestone M5 of the [new-lane plan](PLAN.md), from the design in
[`2026-09-26-gpu-drm-architecture.md`](../research/2026-09-26-gpu-drm-architecture.md) §3.7 and §5
(M5: Vulkan WSI, display + xcb). Builds on [M3](M3-libdrm-phoenix.md) (libdrm-phoenix, the Mesa DRM
build, kmscube and quakespasm-drm passing on the Pi) and the two servers `rpi4-kms` ([M2](M2-kms-server.md))
and `rpi4-v3d-async` ([M1](M1-async-render-server.md)).

**Status (2026-09-27, latest):** first Pi cycle `m5-vkcube` (queue25): **drmprobe-m5 PASS (38/0/1:
G4a + G17 proven on hardware); vkcube enumerated everything, then hung in its first `vkQueueSubmit`** —
Mesa's `sync_merge()` issues a raw `ioctl(SYNC_IOC_MERGE)` on libdrm-phoenix's emulated sync files,
which only a Linux kernel understands. Fixed in libdrm-phoenix (G15 in-process: `-Wl,--wrap=ioctl`
interposer + fence-set sync files), host-tested with a negative control; `vkcube-drm` relinked. Result,
diagnosis and fix in [§9](#9-result--m5-vkcube-queue25-2026-09-27-hang-in-the-first-vkqueuesubmit);
the next cycle `m5b-vkcube` is pre-registered in [§10](#10-pre-registered-pi-cycle-m5b-vkcube).

**Earlier status: builds, links, host-tested; no Pi cycle yet** (pre-registered in §7).
Upstream Mesa 26.2.0's **v3dv** is built as a **static ICD** and linked, with a small loader stand-in
(`phxvk`), libdrm-phoenix and upstream **vkcube** (Vulkan-Tools, Apache-2.0), into one static
binary, `vkcube-drm`: 0 undefined symbols, no old-lane string, no `dlopen`. The two gaps that
blocked `VK_KHR_display` on our stack are closed in libdrm-phoenix (library only, additive):
**G4a**, re-export of an imported BO from the render node (the WSI's `vkGetMemoryFdKHR`), and
**G17**, `SET_CLIENT_CAP(ATOMIC)` not implying universal planes (the WSI would have seen **no**
primary plane). The host tests PASS with a new check for each and FAIL against the previous library
(negative control, both checks). Nothing committed or staged; no server, no old-lane
file (the vkQuake port, the Mesa fork, `tools/.gpu-libs`, shipped binaries) and no sibling repo
touched.

Code: [`tools/gpu-lane/vulkan-drm/`](../../tools/gpu-lane/vulkan-drm/) (new),
[`tools/gpu-lane/mesa-drm/build.sh`](../../tools/gpu-lane/mesa-drm/build.sh) (`--vulkan`, opt-in),
Mesa patches 0010–0011, [`tools/gpu-lane/libdrm-phoenix/`](../../tools/gpu-lane/libdrm-phoenix/) (G4a).

Evidence tags as in M3: **[read]** source, **[built]** the cross build, **[host]** host harness,
**[inferred]** reasoning only.

---

## 0. Decisions at a glance

| Question | Decision |
|---|---|
| How is v3dv built? | `mesa-drm/build.sh --vulkan`: `-Dvulkan-drivers=broadcom`, **no** gallium/EGL/GBM/GLES, in its **own** directory `mesa-drm/build-out-vulkan/` (the default GLES `build-out/` and the `--opengl` dirs are not touched; a dir configured one way refuses the other). `VK_KHR_display` (`wsi_common_display.c`) comes for free: Mesa builds it whenever the system is KMS/DRM, which patch 0001 already says Phoenix is. |
| No loader, no `dlopen` — how does a program reach the driver? | Patch **0010** makes `libvulkan_broadcom` a `library()` (an archive under the cross file's `default_library=static`, unchanged for `shared`). The program links the archive **whole** and asks **`phxvk_GetInstanceProcAddr()`** where it would ask libvulkan's `vkGetInstanceProcAddr()`. `phxvk` forwards every name to the ICD's loader-interface entry point **`vk_icdGetInstanceProcAddr()`** — with one driver there is nothing to dispatch, and Mesa's handles already carry the loader magic. It also does the ICD interface negotiation (v7) once, as a loader would. |
| Why `--whole-archive`? | Mesa's generated dispatch tables reference the driver entry points (`v3dv_*`, `wsi_*`, `vk_common_*`) **weakly**; a weak reference never pulls an archive member, so a normal link would silently leave every entry point whose object nothing else references as NULL. `--gc-sections` still prunes the rest. |
| Demo app | **Upstream vkcube** `vulkan-sdk-1.4.350.0` (headers 1.4.350 ≤ Mesa's 1.4.354), display WSI only (`-DVK_USE_PLATFORM_DISPLAY_KHR`, no xcb/xlib/wayland). vkcube already loads everything through `vkGetInstanceProcAddr`, so its patch is 14 lines (§3.2). SPIR-V shaders are the committed `.inc` files: no glslang. |
| WSI allocation path | v3dv allocates every swapchain image **on the display device** (`device_alloc_for_wsi`: `CREATE_DUMB` on card0, PRIME to the render node) — the M3 **G1** import path, already PASS on the Pi. So **G7** (kms importing a render buffer) is **not** needed for `VK_KHR_display`. |
| What was missing | `wsi_common_drm.c` then asks `vkGetMemoryFdKHR` for the image's dma-buf, and **v3dv exports from the render node** (`v3dv_GetMemoryFdKHR` → `drmPrimeHandleToFD(render_fd)`) — M3's **G4** (`-ENOSYS`). Closed for the case WSI needs, **imported** BOs, in the library: **G4a** (§4). Render-owned exports (G4 proper) stay open; WSI never makes one except a one-time feature probe that fails soft (§5). |
| Present synchronisation | `DMA_BUF_IOCTL_EXPORT/IMPORT_SYNC_FILE` answer `ENOTTY` (G15), so WSI falls back to **driver implicit sync**; on our stack that is M3's **G13**: a flip of a framebuffer whose dumb buffer was imported gets the render BO's last-use fence as `IN_FENCE_FD`, gated by `rpi4-kms -G`. v3dv's queue runs Mesa's `THREADED_ON_DEMAND` submit mode, which stays `IMMEDIATE` unless a submit waits on an unsubmitted timeline point (vkcube never does): `vkQueueSubmit` then calls `v3dv_queue_driver_submit` synchronously, so the `SUBMIT_CL` (and the BO's last-use fence the library mirrors) exists before `vkQueuePresentKHR` commits the flip [read: `vk_queue.c:69-71, 1063-1077`]. |
| Atomic-only client | The WSI sets `DRM_CLIENT_CAP_ATOMIC` only; DRM (`drm_setclientcap`) turns universal planes on with it, rpi4-kms did not (it lists the primary plane only with `UNIVERSAL_PLANES`). Fixed in the library: **G17** (§4.2). |
| Server changes | **None.** |

## 1. What was built

| Path | What |
|---|---|
| `tools/gpu-lane/mesa-drm/build.sh` | new opt-in `--vulkan` (default unchanged): own out dir `build-out-vulkan/`, `mesa-vulkan.txt` label (GL and Vulkan dirs refuse each other), installs `prefix/lib/libvulkan_broadcom.a`, copies Mesa's Vulkan headers to `prefix/include/{vulkan,vk_video}`, writes `vulkan-link.txt`, checks the ICD symbols; `--vulkan` excludes `--opengl`/`--relink` |
| `tools/gpu-lane/mesa-drm/patches/mesa/0010-…patch` | v3dv ICD as an archive (+5/−1) |
| `tools/gpu-lane/mesa-drm/patches/mesa/0011-…patch` | Phoenix OS gates: DRM format modifiers in `vk_image`, build-id-free UUIDs in v3dv (+23/−6) |
| `tools/gpu-lane/mesa-drm/.gitignore` | + `build-out-vulkan/` |
| `tools/gpu-lane/vulkan-drm/build.sh` | Mesa `--vulkan` → libdrm-phoenix snapshot → Vulkan-Tools clone + patch → compile → link → verify |
| `tools/gpu-lane/vulkan-drm/phxvk/phxvk_loader.{c,h}` | the loader stand-in (~190 lines, BSD-3-Clause) |
| `tools/gpu-lane/vulkan-drm/patches/vkcube/0001-…patch` | vkcube on Phoenix (+14/−1) |
| `tools/gpu-lane/vulkan-drm/.gitignore` | `build-out/` |
| `tools/gpu-lane/libdrm-phoenix/src/drm_phoenix_v3d.c`, `drm_phoenix_priv.h` | **G4a** (§4.1), +73/−4 |
| `tools/gpu-lane/libdrm-phoenix/src/drm_phoenix_kms.c` | **G17** (§4.2), +11/−1 |
| `tools/gpu-lane/libdrm-phoenix/drmprobe/drmprobe.c`, `hosttest/run.sh` | new checks `atomic_universal`, `prime_reexport_render` (+72/−2) |

## 2. Build

```
tools/gpu-lane/vulkan-drm/build.sh                 # Mesa v3dv (≈2 min cold at -j16) + vkcube, one static binary
tools/gpu-lane/vulkan-drm/build.sh --skip-mesa     # relink only (≈30 s), e.g. after a libdrm-phoenix change
tools/gpu-lane/vulkan-drm/build.sh --libdrm-prefix tools/gpu-lane/libdrm-phoenix/build-out-<x>/prefix
tools/gpu-lane/mesa-drm/build.sh --vulkan [--libdrm-prefix …]   # the ICD alone
tools/gpu-lane/libdrm-phoenix/build.sh --out tools/gpu-lane/libdrm-phoenix/build-out-m5   # the G4a library (default input)
```

Outputs (all gitignored): `mesa-drm/build-out-vulkan/` (≈1.1 GB: `mesa-src/` = `git clone -s` of
`external/mesa` detached at `mesa-26.2.0` + patches 0001–0011, `mesa-build/`, `prefix/`),
`vulkan-drm/build-out/` (`Vulkan-Tools/`, `libdrm-prefix/` snapshot, `obj/`, `vkcube-drm`,
`vkcube-drm.map`, `vkcube-drm.stripped`, `BUILD-INFO.txt`, logs), `libdrm-phoenix/build-out-m5/`.

* **Mesa options** (`--vulkan`): `-Dgallium-drivers= -Dvulkan-drivers=broadcom -Dvulkan-layers=
  -Dvulkan-beta=false -Degl=disabled -Dgbm=disabled -Dglx=disabled -Dopengl=false -Dgles1=disabled
  -Dgles2=disabled -Dplatforms=`, the rest exactly as the GLES build (`debugoptimized` +
  `b_ndebug=true`, `llvm`/`spirv-tools`/`shader-cache`/`xmlconfig`/`zstd`… off, `--wrap-mode=nodownload`,
  the same cross file incl. `has_function_posix_memalign = false`, the same compat include dir).
  Warnings: the two known ones (`u_math.h:892` `-Wsign-compare` in C++, `u_thread.c:118` `#warning`
  no `pthread_setname_np`); none in patched code. meson: only the known `-mtls-dialect` cross note.
* **The ICD archive** is a real (not thin) archive, 196 MB with debug info: meson bundles the
  internal static libraries (Vulkan runtime + WSI, Vulkan util, NIR, SPIR-V, broadcom compiler/CLE/
  common, util) into the installed archive — no second list needed. `vulkan-link.txt` = ICD,
  libdrm.a, the compat shim, the ports' `libz.a`.
* **vkcube** is compiled directly (`cube.c` + the committed `.inc` SPIR-V + `lunarg.ppm.h`), `gnu11`,
  0 warnings; `phxvk_loader.c` under `-Wall -Wextra -Werror`.
* **Link** (kmscube's shape): C++ driver, `-static`, `--gc-sections`, `-z max-page-size=0x1000`,
  **`-Wl,--wrap=mmap`**, the ICD `--whole-archive`, then one group of `libdrm.a` + compat + `libz.a`.
  Link log empty.
* **Stale-input note:** adding patches 0010/0011 changes the patch-set stamp of **every** mesa-drm out
  dir, so the next default GLES or `--opengl` build re-applies the patches (0001–0009 files get new
  mtimes → a partial rebuild). 0010/0011 touch only `src/broadcom/vulkan` and `src/vulkan/runtime`,
  which a GL build does not compile, so GL binaries are unchanged in content.

## 3. Patches

### 3.1 Mesa (`tools/gpu-lane/mesa-drm/patches/mesa/`, over `mesa-26.2.0`)

| # | Patch | Lines | Seam / rationale |
|---|---|---|---|
| 0010 | `broadcom/vulkan: build the v3dv ICD as an archive on Phoenix` | +5/−1 | The same move as 0002 for the GL stack: no loader, no usable `dlopen` of a driver (libphoenix not PIC, no TLS relocations in `dl.c`), so `shared_library('vulkan_broadcom')` → `library()`, which follows `default_library`. The ICD version script (`vulkan_icd_link_args`) does not apply to an archive. |
| 0011 | `vulkan, v3dv: Phoenix-RTOS OS gates -- DRM format modifiers and a build-id-free UUID` | +23/−6 | **(a)** `vk_image.[ch]`: `drm_format_mod` and the `VK_EXT_image_drm_format_modifier` plumbing were compiled only for `DETECT_OS_LINUX \|\| DETECT_OS_BSD`, yet v3dv uses them unconditionally (`v3dv_image.c:406` — the first build error) and the KMS WSI depends on them: + `DETECT_OS_PHOENIX` in the four gates. **(b)** `init_uuids()` calls `build_id_find_nhdr_for_addr()`, which `util/build_id.h` declares only with `HAVE_DL_ITERATE_PHDR` (the second build error): under `!HAVE_BUILD_ID` the build identity is BLAKE3(`"v3dv " PACKAGE_VERSION MESA_GIT_SHA1`) — deterministic per build, which is all the pipeline-cache UUID, driver UUID and disk-cache key need. Systems with a build-id are unchanged. (The old lane used fixed dummy UUIDs under `__phoenix__`; not taken.) |

**Fork triage (the M3 practice for gallium, done for v3dv):** the old lane's fork has 9 commits over
`mesa-26.2.0` in `src/broadcom/vulkan` + `src/vulkan`; **none is taken**. `9e0d29c9a5f` (Tier-0 link
closure) = 0011's `vk_image` gate (as `defined(__phoenix__)`) plus fixed dummy UUIDs; `969bfa7b1da`
(force `is_shim` = synchronous submit), `7637fc11531` (BO map = winsys CPU VA), `8020cb275a3` /
`0f598ef45f4` (force / un-force `DISABLE_TFU`) are in-process-winsys hooks; `a295a199c12`,
`17ae63cafa0`, `c91c1a0dd20` are #29 copy-path traces (the #29 fix itself was in vkQuake's
`gl_texmgr.c`); `0f7553f932e` (skip a CL job with a NULL tile-state BO, `__phoenix__`-gated) works
around a zero-sized render area that only the old lane's no-WSI vkQuake glue produced — revisit only
if vkQuake-via-WSI hits `handle_cl_job` with `far=0x24`. On this lane v3dv talks to real DRM nodes
through libdrm-phoenix exactly as on Linux. No `mmap` patch (`--wrap=mmap`;
`objdump`: `v3dv_bo_map_unsynchronized` calls `__wrap_mmap` [built]).

### 3.2 vkcube (`tools/gpu-lane/vulkan-drm/patches/vkcube/`, over Vulkan-Tools `1cb3a31`)

| # | Patch | Lines | What |
|---|---|---|---|
| 0001 | `cube: take vkGetInstanceProcAddr from a linked-in ICD on Phoenix-RTOS` | +14/−1 | `cube_functions.h`: under `__phoenix__`, `vkGetInstanceProcAddr = phxvk_GetInstanceProcAddr` instead of `dlopen("libvulkan.so.1")`+`dlsym`, no `dlclose`; `cube.c`: the POSIX `main()` (the display-WSI path) is also built for `__phoenix__` (it was gated on `__linux__ \|\| __FreeBSD__ \|\| …`; without it the link had no `main`). |

### 3.3 phxvk (the loader stand-in)

`phxvk_GetInstanceProcAddr(instance, name)`: first call prints the lane banner and negotiates the ICD
interface (`vk_icdNegotiateLoaderICDInterfaceVersion`, 7 requested); `vkGetInstanceProcAddr` and
`vkGetDeviceProcAddr` resolve to phxvk; every other name goes to `vk_icdGetInstanceProcAddr`.
`vkQueuePresentKHR` (from either lookup) is wrapped to count presents: `phxvk: first present
result=<r>`, `phxvk: run presents=N secs=S fps=F` every 2 s of presenting, `phxvk: exit …` at exit,
and the first non-success `VkResult` of a present. Nothing else is intercepted. It is generic: any
Vulkan program that loads through `vkGetInstanceProcAddr` (volk-style) links the same way; programs
that call `vk*` as link symbols (vkQuake) additionally need exported trampolines — a later step.

## 4. Gaps closed in libdrm-phoenix

### 4.1 G4a — render-node export of an imported BO

The v3dv `VK_KHR_display` swapchain path, per image [read]:

1. `wsi_create_image` → `v3dv_CreateImage` (LINEAR modifier, the plane's `IN_FORMATS` list) →
   `v3dv_AllocateMemory` with the WSI struct → **`device_alloc_for_wsi`**: `CREATE_DUMB 1024 × N` on
   the display fd (card0) → `PRIME_HANDLE_TO_FD` (card0, `/kmsbuf/<h>`) → render `PRIME_FD_TO_HANDLE`
   (**G1** `BO_IMPORT`) → `lseek(SEEK_END)` (**G3**) → `GET_BO_OFFSET`. All PASS on the Pi (M3).
2. `wsi_init_image_dmabuf_fd` → **`v3dv_GetMemoryFdKHR` → `drmPrimeHandleToFD(render_fd, bo->handle)`**
   — was `-ENOSYS` (G4) → `VK_ERROR_OUT_OF_HOST_MEMORY` → swapchain creation fails. **The blocker.**
3. `wsi_display_image_init` → `drmPrimeFDToHandle(card0, dma_buf_fd)` → must return the dumb handle
   `h` (M3 self-import: `/kmsbuf/<h>` of this client's own export) → `drmModeAddFB2WithModifiers`.

**Fix (library, additive):** the render-node BO table remembers where an import came from
(`imp_port`, `imp_cache`, `imp_id`, set in `ioc_prime_import`). `PRIME_HANDLE_TO_FD` on the render
node for an **imported** handle opens the exporter's buffer name again (`/kmsbuf/<id>`, `O_RDONLY`
as E1 requires, `O_CLOEXEC` from the flags) and records it like a card0 export (size known, so a
later import needs no `atSize`). The descriptor names the same pages — what DRM returns for a
re-export of an imported GEM object — and step 3 then resolves it to `h` unchanged. A render-owned
BO still answers `-ENOSYS` (**G4** proper, needs `V3DA_OP_BO_EXPORT` + `/v3dbuf`); an unknown handle
`-ENOENT`. No protocol change, no server change.

**Host test** (`DRMPHX_OUT=tools/gpu-lane/libdrm-phoenix/build-out-m5 tools/gpu-lane/libdrm-phoenix/hosttest/run.sh`) [host]:

```
HOSTTEST libdrm-phoenix checks=134 fails=0 verdict=PASS
DRMPROBE atomic_universal open=1 atomic=0 planes=2 primary=1 ok=1
DRMPROBE prime_reexport_render rc=0 errno=0 path=/kmsbuf/1 card_handle=1/1 same_pages=1 ok=1 gap=0
HOSTE2E legacy verdict=PASS (only the fake-GPU pixel checks failed, as expected)
HOSTE2E dri verdict=PASS (only the fake-GPU pixel checks failed, as expected)
```

The new drmprobe check does exactly steps 2–3: export the imported BO from the render node, import
the descriptor on card0 (must be the original dumb handle), `mmap` it and compare pages with the dumb
mapping. `run.sh` now requires `prime_reexport_render rc=0 … ok=1`. **Negative control:** the same
run against the pre-M5 library (`DRMPHX_OUT=…/build-out-m3p3`) prints `prime_reexport_render rc=-1
errno=38 … gap=1` and both modes `verdict=FAIL (reexport …)`. Everything else is unchanged
(`prime_export_render rc=-1 errno=38 gap=1` = G4 proper, `unaligned_ends=0`, `imports=1
imports_closed=1 deferred_flips=1`). (That control run rewrote `build-out-m3p3/hosttest/*.log`; no
binary there changed.)

### 4.2 G17 — `SET_CLIENT_CAP(ATOMIC)` must imply universal planes

Mesa's display WSI sets **only** `DRM_CLIENT_CAP_ATOMIC` (`wsi_common_display.c:3699, 4287, 4626,
4645`; `UNIVERSAL_PLANES` appears nowhere in it) and relies on DRM's rule that ATOMIC sets
`universal_planes` to the same value (`drm_setclientcap`). rpi4-kms stores the two caps separately
and lists the primary and cursor planes in `GET_PLANE_RESOURCES` only with `UNIVERSAL_PLANES`
(`kms_main.c:1195, 1284-1287`) — and the plane backend has no overlays, so an ATOMIC-only client
saw **zero planes**: `wsi_display_select_primary_plane` finds nothing and vkcube stops at `Cannot
find a plane compatible with the display!`, before any swapchain work. Every earlier Pi client
dodged it (kmscube `-A` and drmprobe set `UNIVERSAL_PLANES` first; SDL and kmscube's legacy path set
neither and never enumerate planes).

**Fix (library, additive):** after a successful `SET_CLIENT_CAP(ATOMIC, v)` on card0,
libdrm-phoenix also sends `SET_CLIENT_CAP(UNIVERSAL_PLANES, v)` — DRM's semantics, one extra round
trip once per client. (DRM also sets `aspect_ratio_allowed`; rpi4-kms stores that cap but uses it
nowhere, so it is not mirrored.) Works with the staged `rpi4-kms-m3p2`; no server change. The same
rule belongs in the server eventually (a non-libdrm client would still see it), noted as a
follow-up.

**Host test:** new drmprobe check `atomic_universal` — a fresh card0 open, ATOMIC only, then
`drmModeGetPlaneResources` + each plane's `type` property: `planes=2 primary=1 ok=1` (above).
**Negative control** against the pre-M5 library: `atomic_universal … atomic=0 planes=0 primary=0
ok=0`, both modes `verdict=FAIL (… atomic_universal)` — the fake kms models rpi4-kms's filter, so
the check sees exactly the hardware behaviour.

## 5. What `VK_KHR_display` needs from the stack, and where each piece stands

| Need (Mesa source) | Status on our stack |
|---|---|
| Device enumeration: `drmGetDevices2` → a platform device with a render node (`v3d`) + a KMS primary (`vc4`) with a connected connector; `KHR_display` enabled at instance creation (`v3dv_device.c:1683-1749`) | ✅ libdrm-phoenix static list, `/dev/dri/card0`, `card1`, `renderD128` (G10) |
| `fstat` on primary + render node, `drmGetVersion(render) = v3d`, `GET_PARAM`s incl. TFU/CSD/CACHE_FLUSH/MULTISYNC/CPU_QUEUE (`device_has_expected_features`) | ✅ G2; the server advertises all five |
| DRM master on the display fd (`local_drmIsMaster` = `drmAuthMagic(fd, 0) != -EACCES`), `DROP_MASTER` | ✅ card0 accepts both as no-ops (else `wsi->fd = -1`: no displays) |
| `SET_CLIENT_CAP(ATOMIC)` **⇒ universal planes** (the WSI never sets `UNIVERSAL_PLANES`) | ✅ **G17 (this pass)**; before it: 0 planes listed |
| resources, connector (+ modes), encoder, CRTC, plane resources, planes, `OBJ_GETPROPERTIES` + `GETPROPERTY`: connector `CRTC_ID`, `DPMS`; CRTC `MODE_ID`, `ACTIVE`; plane `type`, `FB_ID`, `CRTC_ID`, `SRC_*`, `CRTC_*`, `IN_FORMATS` (optional: `GAMMA_LUT`, `DEGAMMA_LUT`, `CTM`, `HDR_OUTPUT_METADATA`, `Colorspace` — absent ⇒ never sent) | ✅ rpi4-kms serves every required property, `IN_FORMATS` with XR24/AR24 × LINEAR |
| Swapchain memory on the display device + import on the render node | ✅ G1, G3 |
| `vkGetMemoryFdKHR` on that memory (render-node export of an import) | ✅ **G4a (this pass)** |
| card0 self-import of the descriptor, `ADDFB2` with `DRM_MODE_FB_MODIFIERS` (LINEAR), 1920×1080 XR24 pitch 7680 on a 1024-px dumb BO | ✅ M3 (self-import, `offset + pitch × height ≤ size`) |
| `CREATEPROPBLOB` (mode), `ATOMIC` with `TEST_ONLY` (swapchain creation), then `NONBLOCK \| PAGE_FLIP_EVENT \| ALLOW_MODESET` (first present: connector `CRTC_ID` + `MODE_ID` + `ACTIVE` + the plane), then `NONBLOCK \| PAGE_FLIP_EVENT` flips | ✅ flattened (host-tested with kmscube `-A`'s first commit, the same shape); **first time on hardware** — no earlier Pi run sent a modeset through `ATOMIC` |
| Flip events with a 64-bit `user_data` (the image pointer) through `drmHandleEvent` on a `poll()`ing thread | ✅ (`kms_proto.h` carries 64 bits); `poll()` rides the 20 ms quantum → **~30 fps expected (G12)** |
| `CRTC_GET_SEQUENCE` / `CRTC_QUEUE_SEQUENCE` (present timing, display events) | ✅ served (vkcube does not register display events) |
| Implicit sync of a flip against the render job | ✅ **G13** in-process (needs `rpi4-kms -G`); `DMA_BUF_*_SYNC_FILE` → `ENOTTY` (G15) ⇒ WSI takes the driver-implicit path, as designed |
| One-time WSI probe `wsi_drm_check_dma_buf_sync_file_import_export`: allocates a 4 KiB **render-owned** BO and asks `vkGetMemoryFdKHR` for it | ⚠ G4 proper → `-ENOSYS` → `FEATURE_NOT_PRESENT` (cached) — **soft**: one expected `PRIME_HANDLE_TO_FD node=render rc=-1 errno=38` trace line, maybe one Mesa warning line |
| Syncobjs on the render fd (fences, semaphores, queue), `SYNCOBJ_WAIT` incl. `WAIT_FOR_SUBMIT` | ✅ |
| `SUBMIT_CPU` (G5) | not used by vkcube (queries / indirect dispatch only) |
| libphoenix: `pthread_condattr_setclock(CLOCK_MONOTONIC)`, `sysconf(_SC_PHYS_PAGES)` (v3dv's heap size — 0 would fail every allocation), `open_memstream` | ✅ present in the tree sysroot (build 10, libc-gaps); linked from libphoenix, not the shim [built] |

**Not needed for this path:** G7 (kms import of a foreign buffer — v3dv allocates on the display
device), G6 (opaque syncobj / cross-process sync-file fds), G8 (out-fence), G4 proper.

**Still open for the rest of M5:** the **xcb** half (`VK_KHR_xcb_surface`) needs M4's Xorg with
DRI3/Present, a Mesa build with `-Dplatforms=x11` (xcb/xshmfence ports), and on the server side G4
(client buffers exported from the render node for DRI3), G6 (explicit-sync or sync-file fds across
processes) and cross-process implicit sync (`BO_LAST_FENCE`); vkQuake via WSI needs exported
`vk*` trampolines on top of phxvk (§3.3) and SDL2's Vulkan KMSDRM path; timestamp/occlusion
queries need G5.

## 6. Verification (the delivered binary) [built]

| Check | Result |
|---|---|
| static link, `nm -u` | link log empty; **0** undefined symbols |
| `size` | text 10 929 990, data 511 280, bss 107 060; file 52 470 520 B, **stripped 11 448 856 B** |
| sha256 (first 16) | `vkcube-drm` `e345e2358ab9d13d`, `vkcube-drm.stripped` `10a20407e36e7111`; inputs: ICD `69c689ad4926672b` (Mesa patch set `60dd139d1e2bb22b`, 11 patches), libdrm.a `d456b57cc3cb8c0c` (`libdrm-phoenix/build-out-m5`: G4a + G17); `build-out/BUILD-INFO.txt` |
| v3dv + runtime | `vk_icdGetInstanceProcAddr`, `vk_icdNegotiateLoaderICDInterfaceVersion`, `v3dv_CreateInstance`, `v3dv_queue_driver_submit`, `vk_common_QueueSubmit2`, `v3dv_CmdDraw`, `v3dv_CreateGraphicsPipelines`, `v3dv_AllocateMemory`, `v3dv_GetMemoryFdKHR`; string `V3D %d.%d.%d.%d` |
| WSI display | `wsi_CreateDisplayPlaneSurfaceKHR`, `wsi_GetPhysicalDeviceDisplayPropertiesKHR`, `wsi_CreateSwapchainKHR`, `wsi_QueuePresentKHR`, `wsi_AcquireNextImage2KHR`, `drmModeAtomicCommit`, `drmCrtcQueueSequence`; strings `VK_KHR_display`, `VK_KHR_swapchain`, `Failed to drmModeObjectGetProperties` |
| libdrm-phoenix | `drm_phoenix_ioctl`, `drmPhoenixMmap`, `__wrap_mmap`; strings `libdrm-phoenix:`, `DRMPHX_TRACE`, `/dev/dri/card0`, `/dev/dri/card1`, `/dev/dri/renderD128`, `/kmsbuf`, `brcm,2711-v3d`; `objdump`: `v3dv_bo_map_unsynchronized` → `bl __wrap_mmap` (plus libphoenix's `malloc`/`fopen`/`pthread_create`, which pass through) |
| phxvk + vkcube | `phxvk_GetInstanceProcAddr`; strings `phxvk: new GPU lane`, `Cannot find a plane compatible with the display` |
| old lane absent | `v3d-winsys:`, `v3da-winsys:`, `phoenix_v3d_ioctl`, `peek_next_scanout`, `v3d-srv`, `V3DV_PHOENIX`, `/dev/fb0`, `RPI4FB_GETMODE`, `pl_phoenix`, `PL_VkHostAllocator`, `vkquake`: all **0** (`build.sh` fails on any) |
| `dlopen` | not linked |

## 7. Pre-registered Pi cycle `m5-vkcube` (one netboot cycle)

**Question:** does an unmodified upstream Vulkan program — vkcube on Mesa's v3dv with its
`VK_KHR_display` WSI — enumerate the display, create a swapchain whose images are rpi4-kms dumb
buffers imported into rpi4-v3d-async, render with v3dv, and present by atomic page flips on HDMI,
through libdrm-phoenix and the two new-lane servers? At what rate?

**Preconditions:** netboot image ≥ build 10 (`sysconf(_SC_PHYS_PAGES)` in libphoenix is linked
statically, so what matters is the binary, but the servers and kernel are the ones kmscube passed
on: build ≥ 9, `core_freq=500`); no GPU app, X, SDL program or `rpi4-v3d` in the boot; the `m3p2`
servers staged (unchanged since `m3p2-drmprobe`/`m3p3b-kmscube`/`m3p4-qsdrm`).

**Stage (coordinator)** (`sudo install -m 755 <source> <path>`, `cmp` afterwards; `<export>` = the live
fsid=0 export, `awk '!/^#/ && /fsid=0/{print $1; exit}' /etc/exports`):

| Source | Export path |
|---|---|
| `tools/gpu-lane/vulkan-drm/build-out/vkcube-drm.stripped` | `<export>/bin/vkcube-drm` |
| `tools/gpu-lane/libdrm-phoenix/build-out-m5/drmprobe` | `<export>/bin/drmprobe-m5` |
| (already staged) `rpi4-v3d-async-m3p2`, `rpi4-kms-m3p2`, `kmstest-m3p2` | — |

Keep the unstripped `build-out/vkcube-drm` on the host for `addr2line`.

**One cycle** (Bash `timeout: 600000`):

```
./scripts/test-cycle-psh-interact.sh --label m5-vkcube --idle-secs 30 --max-cmd-secs 150 \
    --hdmi-dense-on 'phxvk: new GPU lane' -- \
    "/bin/rpi4-v3d-async-m3p2 -r 1 -m serial -i" \
    "/bin/rpi4-kms-m3p2 -G" \
    "/bin/drmprobe-m5 -n 30" \
    "export DRMPHX_TRACE=1" \
    "/bin/vkcube-drm --wsi display --c 60" \
    "export DRMPHX_TRACE=0" \
    "/bin/vkcube-drm --wsi display --c 600" \
    "/bin/kmstest-m3p2 stats"
```

* Order: render server first (`rpi4-kms -G` opens the render fence page at start). Both detach.
* `drmprobe-m5` first: it proves G4a on hardware in isolation (`prime_reexport_render`), so a vkcube
  failure at swapchain creation can be told apart from a library problem.
* `--c N` (two dashes) = vkcube's frame count; it then cleans up and exits. `--wsi display` is
  explicit on purpose: it is the only WSI compiled in and `auto` would also land on it, but an
  explicit choice turns a missing `VK_KHR_display` into vkcube's own clear error instead of a
  silent fallback search.
* `--idle-secs 30`: the first frame compiles SPIR-V → NIR → QPU (several seconds of silence possible);
  after the first present phxvk prints every 2 s. 600 frames ≈ 20 s at 30 fps; `--max-cmd-secs 150`
  covers ~4 fps. Wall clock ≈ netboot 60–150 s + 8 × (30 s idle + run) ≈ 7–8 min.
* **Optional variant** (only if the image carries the `gpu-lane/poll-wake` kernel and
  `rpi4-kms-poll` is staged, [poll-wake](poll-wake.md) §7): the same cycle with `/bin/rpi4-kms-poll -G`
  — predicts 55–60 fps instead of ~30.

**Grade:**

```
grep -a -E '^(phxvk|Selected GPU|DRMPHX|DRMPROBE|KMS|KMSTEST|V3DA|MESA|WSI|Cannot|vkcube|Error|Usage)' \
    artifacts/rpi4b-uart/rpi4b-uart-*-m5-vkcube.log
./scripts/uart-summary.sh m5-vkcube
```

~1.3 % of UART lines are corrupted (re-read, don't count); EL0 dumps print twice; the traced run is
chatty (a few hundred `DRMPHX` lines in its first seconds, rate-limited per request).

**HDMI grading rule:** only snapshots **after** the `(psh)% /bin/vkcube-drm --wsi display --c 60`
echo / the `phxvk: new GPU lane` line count (dense ticks start there). Earlier ticks show the console
during the server and drmprobe commands and say nothing about vkcube.

**Predictions** (first vkcube = traced, `--c 60`):

| Line / observation | Predicted | If instead… |
|---|---|---|
| `DRMPROBE atomic_universal open=1 atomic=0 planes=2 primary=1 ok=1` | **G17 on hardware**: an ATOMIC-only client sees the primary (+ cursor) plane | `planes=0`: a pre-M5 drmprobe staged (`cmp`) — vkcube would stop at `Cannot find a plane compatible …` |
| `DRMPROBE prime_reexport_render rc=0 errno=0 path=/kmsbuf/<h> card_handle=<h>/<h> same_pages=1 ok=1 gap=0`, `DRMPROBE RESULT pass=38 fail=0 gap=1 … verdict=PASS` | G4a on hardware (the gap = `prime_export_render`, G4 proper; pass = m3p2's 36 + the two new checks) | `rc=-1 errno=38`: a pre-M5 drmprobe staged (`cmp`); `card_handle` ≠ `<h>`: card0 self-import broke; `same_pages=0`: the reopened name maps other pages — **blocker**, stop. |
| `phxvk: new GPU lane -- Mesa 26.2 v3dv (static ICD) + VK_KHR_display …`, `phxvk: ICD interface version 7 (negotiate result=0)` | once per vkcube | no banner: wrong binary staged. |
| `DRMPHX conn … path=/dev/dri/card0 node=card0 …`, `… /dev/dri/card1 node=card1 …`, `… /dev/dri/renderD128 node=render …`; `DRM_IOCTL_V3D_GET_PARAM` lines | v3dv's enumeration: display fd, primary, render (in that order: vc4 is device 0) | no `card0` conn: `try_display_device` refused card0 (`KHR_display` not enabled, or no connected connector) → `Cannot find any mode`/no displays later. |
| `Selected GPU 0: V3D 4.2.14.0, type: IntegratedGpu` (stderr) | the v3dv physical device | `vkEnumeratePhysicalDevices` 0 devices → vkcube `ERR_EXIT` ("Cannot find a compatible Vulkan installable client driver (ICD)"): read the `MESA` lines — `Kernel driver doesn't have required features` (a `GET_PARAM` answered 0), `failed to stat DRM …` (G2), `Device version < 42`. |
| `DRMPHX ioctl node=card0 … DRM_IOCTL_AUTH_MAGIC rc=0`, `… SET_CLIENT_CAP … cap=0x3 value=0x1 rc=0`, `… DROP_MASTER rc=0`; then `DRM_IOCTL_MODE_GETPLANERESOURCES rc=0` followed by `GETPLANE` lines | the WSI master check + atomic cap (the implied `UNIVERSAL_PLANES` is inside the same ioctl, G17) + plane enumeration | `AUTH_MAGIC errno=13`: the display is treated as non-master → no displays; no `GETPLANE` after `GETPLANERESOURCES`: 0 planes (G17 missing: stale libdrm inside the binary, check `BUILD-INFO.txt`). |
| `DRM_IOCTL_MODE_GETPLANERESOURCES`, `GETPLANE`, `OBJ_GETPROPERTIES`, `GETPROPERTY`, `GETPROPBLOB` (IN_FORMATS) lines, all `rc=0` | display / plane / modifier enumeration | a `MESA` debug line `Failed to find required property …` is only at debug level; symptom = `Cannot find a plane compatible with the display!` (vkcube) → compare the property names in §5. |
| **One** `DRM_IOCTL_V3D_CREATE_BO … size=4096` followed by `DRM_IOCTL_PRIME_HANDLE_TO_FD node=render … rc=-1 errno=38` (possibly one `MESA` warning about `VK_ERROR_OUT_OF_HOST_MEMORY`) | the WSI dma-buf sync-file feature probe on a render-owned BO: **G4 proper, expected, soft** (cached → implicit sync) | more than one such line: something else exports render-owned memory — note what follows it. |
| ×3: `MODE_CREATE_DUMB … w=1024 h=2025 bpp=32 … pitch=4096 size=8294400` → `PRIME_HANDLE_TO_FD node=card0 rc=0 … fdpath=/kmsbuf/<h>` → `PRIME_FD_TO_HANDLE node=render rc=0 … fdpath=/kmsbuf/<h>` + `V3DA srv import … ns=kmsbuf id=<h> pages=2025 … contiguous=1` → `V3D_GET_BO_OFFSET rc=0` → **`PRIME_HANDLE_TO_FD node=render rc=0 … fdpath=/kmsbuf/<h>`** (G4a) → `PRIME_FD_TO_HANDLE node=card0 rc=0 handle=<h>` → `MODE_ADDFB2 rc=0 1920x1080 fmt=0x34325258 flags=0x2 handle=<h> pitch=7680 offset=0 mod=0x0 → fb=<f>` | the swapchain (3 images: vkcube asks 3, WSI minimum 2), ≈ 24.9 MB of the 32 MiB kms pool | `CREATE_DUMB rc=-1 errno=12/28`: pool exhausted → re-run kms with `-p 48`; render `PRIME_FD_TO_HANDLE rc=-1`: G1 (errno as in M3); **render `PRIME_HANDLE_TO_FD rc=-1 errno=38` on an imported handle**: a pre-M5 libdrm-phoenix inside the binary (`BUILD-INFO.txt`); card0 `PRIME_FD_TO_HANDLE errno=38`: the descriptor did not resolve to this client's export (would need G7); `ADDFB2 errno=22`: pitch/size or the modifier flag. |
| `DRM_IOCTL_MODE_CREATEPROPBLOB rc=0` + `DRM_IOCTL_MODE_ATOMIC rc=0 … flags=0x700` (TEST_ONLY \| NONBLOCK \| ALLOW_MODESET) | swapchain-creation test commit(s) | `rc=-1 errno=22`: the flattened modeset commit is invalid for rpi4-kms (mode blob ≠ current mode, or the connector/CRTC mapping) → vkcube fails at swapchain creation (`VK_ERROR_…` from `vkCreateSwapchainKHR`); the `KMS` validation line names the field. |
| `DRM_IOCTL_V3D_SUBMIT_CL rc=0 …` after the shader compile; then `DRM_IOCTL_MODE_ATOMIC rc=0 … flags=0x601` (first present: modeset + event) and `flags=0x201` flips (first 16, then every 256th) | render + present | `SUBMIT_CL rc=-22`: submit layout (compare kmscube's); first `ATOMIC errno=22`: as the row above; `errno=16`: a flip while one pending. |
| `phxvk: first present result=0`, then `phxvk: run presents=N secs=2.0x fps=F` | **F ≈ 30** (G12: the WSI's event thread `poll()`s card0 for every flip event; FIFO with 3 images paces presents to flip events), up to 60 if events align | F < 20 with the GPU mostly idle: per-frame IPC — compare `V3DA` stats; `present n=… result=-1000001004` (OUT_OF_DATE) / `-1000000000` (SURFACE_LOST): the flip path reported an error — the preceding `DRMPHX … ATOMIC` line has it; no `first present` within `--idle-secs`: a wait never completes (acquire waits for a flip event that never came: `KMS` event lines) or the first-frame compile hangs. |
| **HDMI** (dense after the banner) | **vkcube's rotating textured cube** (the LunarG logo on each face) on a dark grey clear, full screen 1920×1080, no console text over it; the console returns after exit | console still visible with presents counting: the modeset commit did not take (`KMS` apply lines); black with presents counting: the GPU wrote other pages (`V3DA srv import pa0` vs `KMS pool pa`); garbled stripes: a tiled image was scanned out (the WSI asked LINEAR — report it); torn / half-drawn cube: G13 did not gate (a `libdrm-phoenix: rpi4-kms runs without -G` line). |
| exit of each run: `phxvk: exit presents=60 …` / `presents=600 …`, prompt returns; up to 3 × `MESA: error: destroy dumb object <h>: …` | clean exit. The error lines are **upstream behaviour**: the WSI closes the card0 handle (`GEM_CLOSE`), then v3dv's `device_free_wsi_dumb` destroys the same handle again — identical on Linux (same fd) | a fault at exit: `addr2line` it; missing `V3DA srv import released …` ×3 per run: an import leaked. |
| second run (`--c 600`, untraced) | the same without `DRMPHX` lines; `phxvk: run … fps≈30` steady, `exit presents=600` | failing only here: server-side leaks from the first process (next row). |
| `KMSTEST stats … bos=0 exports=0 apply_errors=0` | nothing left after two vkcube processes | `bos>0`/`exports>0`: dumb BOs outlive their client (the double close above must not matter: client death releases all). |
| fault dumps (`uart-summary.sh`) | 0 kernel, 0 EL0 | an EL0 fault in vkcube: `.toolchain/aarch64-phoenix/bin/aarch64-phoenix-addr2line -f -i -e tools/gpu-lane/vulkan-drm/build-out/vkcube-drm <pc>`. |

**What the cycle decides:** a spinning cube on HDMI with `phxvk: exit presents=600` = **the first
Vulkan program on the DRM-shaped stack, with the standard `VK_KHR_display` WSI** (M5 display half);
next: vkmark / vkQuake through phxvk + trampolines, and the xcb half after M4. A failure before
`Selected GPU` is enumeration (libdrm-phoenix identity, G2, `GET_PARAM`); at swapchain creation it is
the §5 chain (the first `rc=-1` DRMPHX line names the link); a correct but ~30 fps cube is G12, not
a render problem.

## 8. Risks and open questions (only the Pi can answer)

* **First modeset through `ATOMIC` on hardware** — kmscube and SDL used `SETCRTC`; drmprobe's atomic
  test was `FB_ID` only. The flattening of this exact shape is host-tested, the server path is not.
* **First v3dv on this lane** — v3dv itself ran on V3D 4.2 in the old lane (vkQuake) through an
  in-process winsys; here every ioctl is a server message and the queue uses real syncobjs (the old
  lane forced synchronous submit). Every submit that waits on semaphores first does a zero-timeout
  `VK_SYNC_WAIT_PENDING` (`vk_queue.c:1063-1074`); with `DRM_CAP_SYNCOBJ_TIMELINE = 0` Mesa does that
  through `spin_wait_for_sync_file` (`SYNCOBJ_HANDLE_TO_FD(EXPORT_SYNC_FILE)` until it succeeds —
  never through the NULL `timeline_wait`), i.e. one in-process sync-file export (a `dup()` of the
  render descriptor + `close`) per present-time submit. Correct (an empty syncobj answers `-EINVAL`,
  as DRM), a small per-frame cost; if that wait ever times out, Mesa starts a submit thread.
* **Swapchain images are LINEAR render targets** — standard for v3dv on Linux (KMS scan-out), but
  never exercised on Phoenix; performance of linear TLB stores at 1080p is part of the fps answer.
* **Memory:** v3dv's heap is a percentage of `sysconf(_SC_PHYS_PAGES)`; with 0 every allocation
  fails `VK_ERROR_OUT_OF_DEVICE_MEMORY` (a pre-build-10 libphoenix — not this binary).
* **kms pool:** 3 × 8.3 MB of 32 MiB — a 4th image (another app asking `minImageCount + 2`) needs `-p 48`.

## 9. Result — `m5-vkcube` (queue25, 2026-09-27): hang in the first `vkQueueSubmit`

Log `artifacts/rpi4b-uart/rpi4b-uart-20260927-070924-m5-vkcube.log` (`grep -a`).

**What passed (as pre-registered):** `drmprobe-m5` → `DRMPROBE RESULT pass=38 fail=0 gap=1 verdict=PASS`
(`atomic_universal … planes=2 primary=1 ok=1` = **G17**, `prime_reexport_render … ok=1` = **G4a**, on
hardware). vkcube: phxvk banner, `ICD interface version 7 (negotiate result=0)`, connections to card0,
card1 and renderD128 in the predicted order, 11 `V3D_GET_PARAM`s, the syncobj probe,
`Selected GPU 0: V3D 4.2.14.0, type: IntegratedGpu`, `AUTH_MAGIC`, `SET_CLIENT_CAP cap=0x3`,
`DROP_MASTER`, connector/CRTC/plane/property/blob enumeration (all `rc=0`), then vkcube's prepare
phase: BOs, 4 queue syncobjs (handles 1–4, created signalled), texture staging, 5 more syncobjs, one
`V3D_SUBMIT_TFU rc=0 … flags=0x2` (the staging-buffer → texture copy, MULTI_SYNC), then
`SYNCOBJ_HANDLE_TO_FD handle=1 flags=0x1 fd=6` and `handle=2 … fd=7` — and **nothing more**: no third
export, no vkcube assertion, no prompt; the second vkcube command never started (psh was still
waiting for the first). Server: `qstat … tfu=1 … err=0 wedges=0` — the TFU job completed.
The swapchain was never reached: vkcube creates it after `demo_prepare`'s first submit.

### 9.1 Where it blocks [read + built]

vkcube's `demo_flush_init_cmd` (`cube.c:825`) submits the prepare command buffer **with a fence**.
`vkQueueSubmit` → `vk_common_QueueSubmit2` → `vk_queue_submit` (IMMEDIATE mode) →
`v3dv_queue_driver_submit` (`v3dv_queue.c:1078`): the TFU job goes out, then — because the submit has a
signal operation (the fence) — **`merge_syncobjs()`** (`v3dv_queue.c:1028`) folds the last-job syncobjs
of **all four** v3dv queues into the fence's syncobj:

```
for each queue sync:  drmSyncobjExportSyncFile(render_fd, sync, &queue_fd)   <- fd 6 (handle 1), fd 7 (handle 2)
                      accum = sync_merge("v3dv_merged_fence", accum, queue_fd) <- HERE, after the 2nd export
drmSyncobjImportSyncFile(render_fd, dst, accum)
```

`sync_merge()` (`util/libsync.h:151`) is `do { ioctl(fd1, SYNC_IOC_MERGE, &data); } while (ret == -1 &&
(errno == EINTR || errno == EAGAIN));` — a **raw `ioctl()`**, not `drmIoctl()`, so libdrm-phoenix never
sees it (hence no trace line). The trace pins it: the loop's second export returned (`fd=7`) and the
third export never came. `objdump` of the binary: `v3dv_queue_driver_submit` calls `ioctl` directly
(2 sites, with the `__errno_location` retry loop). vkcube asserts on every `VkResult`
(`assert(!err)` compiled in, stderr unbuffered — the `Selected GPU` line proves it reaches the UART), and
no assertion appeared: **`vkQueueSubmit` never returned**.

### 9.2 Why

libdrm-phoenix's sync files are an **in-process emulation** (M3 §2.8): `SYNCOBJ_HANDLE_TO_FD(EXPORT_SYNC_FILE)`
returns a `dup()` of the render node descriptor plus a fence snapshot in a process table. That serves
everything that goes back through libdrm (`SYNCOBJ_FD_TO_HANDLE`, kms `IN_FENCE_FD`), which is all M3's
clients needed. But Linux sync files are **kernel objects with their own ioctls** (`SYNC_IOC_MERGE`,
`SYNC_IOC_FILE_INFO`), and Mesa calls those directly — M3 listed this as gap **G15** ("reach the render
server as unknown ioctls, `-ENOTTY`") and predicted only EGL native-fence merging would need it. **v3dv
needs it on every signalling submit**, so the first `vkQueueSubmit` with a fence is the first time the
raw ioctl ran on hardware.

On the dup'ed descriptor the request goes (kernel `posix_ioctl` → `mtDevCtl`) to **rpi4-v3d-async**,
whose ioctl handler knows only `HELLO` and, by its source, answers everything else `-ENOTTY`
(`v3da_main.c:434`). Had that answer arrived, `merge_syncobjs` would have returned
`VK_ERROR_DEVICE_LOST` and vkcube would have printed its assertion — it did not, so on hardware the
request **did not come back** (or came back `EINTR`/`EAGAIN` forever, which libsync retries without
bound). Which of the two is not decidable from the log or the source (the kernel/server path reads
correct; a dup'ed render descriptor's only earlier ioctl was `HELLO`, answered on the same path); it
does not change the fix: **a sync_file ioctl must never leave the process** — Phoenix has no kernel
sync files, so any server that receives one can only answer "unknown", and v3dv needs a real merge, not
an error. (Open follow-up, server/kernel side: send an unknown ioctl to rpi4-v3d-async through a dup'ed
descriptor in a probe and see whether it returns — a latent hazard for any future raw ioctl.)

### 9.3 Fix: G15 in-process — `--wrap=ioctl` and fence-set sync files (libdrm-phoenix, additive)

| Where | Change |
|---|---|
| `src/drm_phoenix_wrap_ioctl.c` (new, 172 lines, own archive member) | **`__wrap_ioctl`**: a program linked with `-Wl,--wrap=ioctl` sends every `ioctl()` here. `SYNC_IOC_MERGE` / `SYNC_IOC_FILE_INFO` (matched on type `'>'`, number 3/4 and the struct size — both `_IOC` encodings) are answered **in-process**: on an emulated sync file, merge → a new emulated sync file (`O_CLOEXEC`) holding both fence sets, info → status (1 signalled / 0 active) + fence count (+ per-fence records when asked); on **any other** descriptor `-ENOTTY` locally, as Linux answers for a non-sync-file — the request never reaches a server. Everything else → `__real_ioctl` unchanged. Its own member, like `drm_phoenix_wrap.c` (it references `__real_ioctl`, which exists only under `--wrap=ioctl`): binaries linked with `--wrap=mmap` alone (kmscube, quakespasm-drm, Xorg-drm, drmprobe-m5) are unaffected. `DRMPHX_TRACE` logs `DRMPHX sync  fd=… nr=3 rc=0 merged_fd=…`. |
| `src/xf86drm_phoenix.c` | a sync file now holds a **fence set** (up to 8 distinct `{slot, queue, gen}` timelines; within one timeline the higher seqno implies the lower, so a merge keeps the max). `drmphx_syncfile_get()` — the one-fence view that syncobj import and kms `IN_FENCE_FD` need — drops signalled fences (fence page, no IPC) and, if more than one is still pending (fences on **different** GPU queues: a DRM syncobj holds one), **CPU-waits all but one** before handing out the last. New `drmphx_syncfile_is/merge/status`. A merged descriptor is another `dup()` of the render node. |
| `src/drm_phoenix_v3d.c`, `src/drm_phoenix_priv.h` | public `drmphx_v3d_fence_signaled/_wait` (the fence-page check and the bounded-slice wait) for the set logic; declarations |
| `patches/0004-meson-phoenix-ioctl-interposer.patch` (new, +1) | builds the new file |
| `build.sh` | `drmprobe` now links `-Wl,--wrap=ioctl` |
| `drmprobe/drmprobe.c`, `hosttest/run.sh`, `hosttest/mock/{fake.c,phx_mock.h}` | new check **`sync_merge`**: a pending CL job's syncobj and a signalled one exported as sync files, raw `ioctl(SYNC_IOC_MERGE)` (Mesa's exact call), `ioctl(SYNC_IOC_FILE_INFO)` on the result, import into a syncobj, wait. The host mock routes `ioctl()` through `__wrap_ioctl` exactly as the Pi link does. |
| `tools/gpu-lane/vulkan-drm/build.sh` | vkcube links `-Wl,--wrap=ioctl` (default library `build-out-m5b`); the verification now **fails the build if any code calls the real `ioctl` except `__wrap_ioctl`** (objdump: the callers of `__wrap_ioctl` are `v3dv_queue_driver_submit` = `merge_syncobjs`, `vk_drm_syncobj_copy_payloads` and `wsi_create_sync_for_image_syncobj` — Mesa's other two `sync_merge` users — plus libdrm-phoenix's HELLOs and libphoenix `tcgetattr`) |

Why the library and not a Mesa patch: `sync_merge` has five callers in the Vulkan runtime, v3dv, WSI and
gallium (`v3d_fence.c`, `dri2.c` for EGL native fences); one interposer covers all with no Mesa change,
the same pattern as `--wrap=mmap` (M3 §2.6).

Cost/limits: a merge = one `dup()` + table work, no IPC; `FILE_INFO` of an active set and a
multi-queue import touch the fence page (a first use of a new descriptor number costs one `HELLO`).
The CPU wait happens only when a merged set still has **two or more unsignalled fences on different
queues** at import time (vkcube per frame: only the render queue is pending; the TFU/CSD/CPU queue
syncs are signalled). `poll()` on an emulated sync file is still G15 (nothing on this path polls one);
a sync file duplicated by the program itself (`os_dupfd_cloexec`, v3dv's perfmon-query path only) is
not in the table — its sync_file ioctls now fail fast with `-ENOTTY` instead of hanging. Cross-process
sync files stay G6.

### 9.4 Host tests [host]

`DRMPHX_OUT=tools/gpu-lane/libdrm-phoenix/build-out-m5b tools/gpu-lane/libdrm-phoenix/hosttest/run.sh`:

```
HOSTTEST libdrm-phoenix checks=134 fails=0 verdict=PASS
DRMPROBE sync_merge setup=0 merge=0 errno=0 mfd=8 info=0 status=0 nfences=1 import=0 wait=0 ok=1 gap=0
HOSTE2E legacy verdict=PASS (only the fake-GPU pixel checks failed, as expected)
DRMPROBE sync_merge setup=0 merge=0 errno=0 mfd=9 info=0 status=0 nfences=1 import=0 wait=0 ok=1 gap=0
HOSTE2E dri verdict=PASS (only the fake-GPU pixel checks failed, as expected)
```

(`status=0`: the fake GPU completes jobs lazily, so the merged set is still active — it exercises the
fence-page path; `atomic_universal` and `prime_reexport_render` still `ok=1`; `unaligned_ends=0`,
`imports=1 imports_closed=1 deferred_flips=1` unchanged.) **Negative control:** the same tree with the
in-process answer disabled (the interposer answers `-ENOTTY` for every sync request) →
`sync_merge … merge=-1 errno=25 … ok=0`, both modes `verdict=FAIL (sync_merge …)`. Not host-tested:
the multi-queue reduction (drmprobe has no TFU/CSD job, so its merges stay on one timeline). Note: the
host test now needs a build dir with `drm_phoenix_wrap_ioctl.c` (`build-out-m5b` or later); older dirs
fail to link the e2e by design.

### 9.5 Builds (new outputs; earlier ones kept)

| Artifact | Path | sha256 (first 16) |
|---|---|---|
| libdrm-phoenix (G4a + G17 + G15 in-process) | `tools/gpu-lane/libdrm-phoenix/build-out-m5b/prefix/lib/libdrm.a` | `a508e207a8d293cf` |
| drmprobe (`--wrap=mmap --wrap=ioctl`) | `tools/gpu-lane/libdrm-phoenix/build-out-m5b/drmprobe` | `83f5caf3a6e52556` |
| **vkcube-drm** (Mesa set `60dd139d1e2bb22b`, ICD `69c689ad4926672b` unchanged, libdrm m5b) | `tools/gpu-lane/vulkan-drm/build-out/vkcube-drm.stripped` / unstripped `vkcube-drm` | `a24709236d050597` / `0952d5209f2a1c4a` |

`nm -u` 0, old-lane strings 0, `__wrap_ioctl` + `__wrap_mmap` linked, the real `ioctl` called only from
`__wrap_ioctl`; libdrm backend 0 warnings (the 9 upstream ones). The m5 binaries (`vkcube-drm` sha
`10a20407…`, `drmprobe-m5`) are superseded but still on disk.

**Other new-lane binaries:** kmscube / quakespasm-drm / Xorg-drm do not link `--wrap=ioctl`; their GL
paths call `sync_merge` only for EGL native-fence merging (`kmscube -A`, not run yet) — relink them with
`--wrap=ioctl` before any cycle that uses `EGL_ANDROID_native_fence_sync`.

## 10. Pre-registered Pi cycle `m5b-vkcube`

**Question:** with Mesa's sync_file ioctls answered in-process, does vkcube get through its first
signalling submit, build the `VK_KHR_display` swapchain (G4a, G17 now proven) and present on HDMI?

**Preconditions:** as §7 (same image, the staged `-m3p2` servers — no server changed).

**Stage (coordinator)** — new names, the m5 binaries stay:

| Source | Export path |
|---|---|
| `tools/gpu-lane/vulkan-drm/build-out/vkcube-drm.stripped` | `<export>/bin/vkcube-drm-m5b` |
| `tools/gpu-lane/libdrm-phoenix/build-out-m5b/drmprobe` | `<export>/bin/drmprobe-m5b` |

(`sudo install -m 755`, then `cmp`; keep the unstripped `vkcube-drm` for `addr2line`.)

**One cycle** (Bash `timeout: 600000`):

```
./scripts/test-cycle-psh-interact.sh --label m5b-vkcube --idle-secs 30 --max-cmd-secs 150 \
    --hdmi-dense-on 'phxvk: new GPU lane' -- \
    "/bin/rpi4-v3d-async-m3p2 -r 1 -m serial -i" \
    "/bin/rpi4-kms-m3p2 -G" \
    "/bin/drmprobe-m5b -n 30" \
    "export DRMPHX_TRACE=1" \
    "/bin/vkcube-drm-m5b --wsi display --c 60" \
    "export DRMPHX_TRACE=0" \
    "/bin/vkcube-drm-m5b --wsi display --c 600" \
    "/bin/kmstest-m3p2 stats"
```

Grade as §7 (add `DRMPHX sync` to the grep: `'^(phxvk|Selected GPU|DRMPHX|DRMPROBE|KMS|KMSTEST|V3DA|MESA|WSI|Cannot|vkcube|Assertion|Error|Usage)'`).
HDMI rule unchanged: only snapshots after the `(psh)% /bin/vkcube-drm-m5b --wsi display --c 60` echo.

**Predictions** — §7's table holds from the `CREATE_DUMB` row on (the rows before it were confirmed by
`m5-vkcube`), with these changes/additions:

| Line / observation | Predicted | If instead… |
|---|---|---|
| `DRMPROBE sync_merge setup=0 merge=0 errno=0 mfd=<n> info=0 status=0\|1 nfences=1 import=0 wait=0 ok=1 gap=0`; `DRMPROBE RESULT pass=39 fail=0 gap=1 … verdict=PASS` | G15 in-process on hardware (pass = m5's 38 + `sync_merge`) | `merge=-1 errno=25 gap=1`: a drmprobe without `--wrap=ioctl` staged (`cmp`); the probe **hangs** at this line: the interposer is not in the binary — stop. |
| after `V3D_SUBMIT_TFU … flags=0x2`: `SYNCOBJ_HANDLE_TO_FD handle=1..4 … flags=0x1`, interleaved with **`DRMPHX sync  fd=<a> nr=3 rc=0 merged_fd=<m>`** ×3, then `SYNCOBJ_FD_TO_HANDLE … flags=0x1 fd=<m>` (import into the fence) and `SYNCOBJ_WAIT … count=1` (vkcube's `vkWaitForFences`) | **the m5 hang point passes**: 4 exports, 3 in-process merges, 1 import per signalling submit | nothing after the 2nd export again: the binary lacks the interposer (`BUILD-INFO.txt`, `vkcube-drm-m5b` sha `a2470923…`); `nr=3 rc=-22`: `flags`/`pad` non-zero in Mesa's request (read the struct); `FD_TO_HANDLE rc=-1 errno=22`: the merged fd was not found in the sync-file table (ring overflow — report). |
| `Assertion '!err' failed in file …cube.c:826` | absent | the submit now fails instead of hanging: the preceding `DRMPHX` line with `rc=-1` names the step. |
| the §7 swapchain rows: `MODE_CREATE_DUMB … w=1024 h=2025` ×3, card0 export → render import (+ `V3DA srv import …`) → render re-export (G4a) → card0 self-import → `MODE_ADDFB2 … flags=0x2 … mod=0x0`; `CREATEPROPBLOB`; `ATOMIC … flags=0x700` (TEST_ONLY) | as §7 | as §7 |
| per frame: `DRMPHX sync … nr=3` lines (first 16 of each request are traced, then the rate limit hides them), `V3D_SUBMIT_CL rc=0`, `ATOMIC … flags=0x601` then `0x201` | render + present | as §7 |
| `phxvk: first present result=0`, `phxvk: run presents=N … fps=F` every 2 s, `phxvk: exit presents=60 …` / `600 …` | **F ≈ 30** (G12), as §7 | as §7; additionally F < 20 with `V3DA` mostly idle: look for the CPU-wait reduction (it only runs when ≥ 2 queues are pending at import — should not happen per frame). |
| HDMI | the rotating textured cube (LunarG logo), full screen | as §7 |
| `KMSTEST stats … bos=0 exports=0`, 0 faults | as §7 | as §7 |

**What the cycle decides:** the cube on HDMI with `phxvk: exit presents=600` = M5's display half done
(then the swapchain/present rows of §7 are graded for the first time). A new stop point is named by
the first `rc=-1` `DRMPHX` line after the last successful one.

### Result — m5b (queue28, 2026-09-27 07:35–07:41): **PASS — vkcube renders through v3dv + VK_KHR_display**

Log `artifacts/rpi4b-uart/rpi4b-uart-20260927-073537-m5b-vkcube.log`. `drmprobe-m5b`: `sync_merge … ok=1`,
`RESULT pass=39 fail=0 gap=1`. vkcube: the sync-file merges answered in-process (`DRMPHX sync fd=… nr=3
rc=0 merged_fd=…`), three swapchain images allocated on card0 and imported by the render server
(`V3DA srv import … pages=2025` ×3), `phxvk: first present result=0`; untraced run **600 frames in 12.29 s =
48.8 fps** (against rpi4-kms-m3p2, before the deferred-flip wake fix — rerun against rpi4-kms-gate is the
next measurement). 0 exceptions. HDMI (`artifacts/hdmi/20260927-074033-m5b-vkcube-tick.png`): the textured
LunarG cube, rotating. Same early-start caveat as m4c.

### Result — m5c (queue29, 2026-09-27 09:02): **vkcube at display rate — 60.15 fps**

Same `vkcube-drm-m5b`, now against the deferred-flip-fixed `rpi4-kms-gate -G`: **600 presents in 9.97 s =
60.15 fps**, `KMS srv flipstat … vbl1=599 vbl2=0` — every flip on its first vblank (48.8 fps before the
fix). 0 exceptions.
