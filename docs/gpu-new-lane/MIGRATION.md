# Migration — every GPU user onto the new lane, then delete the old lane

Last step of the [new-lane plan](PLAN.md) (ground rule 7: "all GPU users move in one planned migration
with the full showcase gate, then the old winsys, scanout hooks and kdrive DDX are removed"). The owner's
end state (directive 2026-09-26): **every GPU user runs on the DRM-shaped stack** — `rpi4-kms` (card0) +
`rpi4-v3d-async` (renderD128) + libdrm-phoenix + upstream Mesa 26.2 (GBM/EGL/GLES/GL, v3dv) + SDL 2.30
KMSDRM + Xorg modesetting/glamor — **and the old lane is gone**: no in-process winsys, no `rpi4-v3d`
daemon, no kdrive `Xphoenix`, no SDL `/dev/fb0` video backend, no Mesa fork.

**Status (2026-09-27):** migration prep. The three game clones that were still missing now **build**:
`quake2-drm`, `quake3-drm`, `vkquake-drm` (§2, all static checks pass, **no Pi cycle yet** — pre-registered
in §6). With `quakespasm-drm` and `stk-drm` (Pi-proven) every showcase game has a new-lane binary; the X
desktop has `Xorg-drm` (Pi-proven for Window Maker and a DRI3/Present GL client) but not yet the showcase
`action` layout (§1, §3). Nothing staged; no old-lane file, port recipe or shipped binary touched.
**Update 2026-09-27 (later):** `quakespasm-drm` now prints the gate's `flipstat` counter (the shared
`gamedrm` hooks, §6.4 `mig-qs` pre-registered), and the two libphoenix gaps vkquake-drm bridged —
`struct ipv6_mreq` and `<execinfo.h>` — are implemented on sibling branches `feat/ipv6mreq-execinfo`
(libphoenix + tests + a ports guard, §3), not yet merged.
**Update 2026-09-27 (adoption, after build 17):** the two Pi-proven performance fixes are now in the
**default** clone builds — the SDL KMSDRM swap reorder (`sdl2-drm/patches/0009`, [frame-pacing.md](frame-pacing.md):
quake2-drm 30.00 → 60.00, quakespasm-drm 30.0 → 46.1 fps) and vkQuake's `r_oit 0` + RGBA8 colour buffer
(`patches-vkquake/0006`, `0007`, [vkquake-perf.md](vkquake-perf.md): 10.4 → 17.1 fps). Every SDL clone was
rebuilt/relinked into its default `build-out/` dir (§2, §6 staging table with the new shas); combined Pi
check `mig-all` pre-registered (§6.5).

Evidence tags as elsewhere: **[Pi]** measured on hardware, **[built]** cross build / link / static check,
**[read]** read in source, **[inferred]** reasoning only.

---

## 1. Inventory — every GPU user on the image

Found by scanning every `bin/`, `usr/bin/`, `sbin/`, `usr/sbin/` binary of the staged rootfs
(`.buildroot/_fs/aarch64a72-generic-rpi4b/root`) for the old lane's strings (`v3d-winsys:`, `phxgl`,
`/dev/fb0`, `RPI4FB_GETMODE`, `/dev/v3d`, `glamor`, `Mesa `) [built]. The only SDL2 ports are the four
GL games (`depends="sdl2"`: quakespasm, quake3, yquake2, supertuxkart) [read].

| User | Old lane (today) | New-lane binary | Status | Remaining before it can replace the old one |
|---|---|---|---|---|
| **GLQuake** | `/usr/bin/quakespasm`: ports SDL2 (`/dev/fb0` video backend) + `sdl_phoenix_glctx` + libGL-phoenix + in-process winsys | `quakespasm-drm` (`sdl2-drm/build.sh`: SDL KMSDRM + Mesa GBM/EGL desktop GL) | ✅ [Pi] 30.9 fps timedemo (`rpi4-kms-gate`, poll-wake) vs old lane 33.4; `flipstat` counter [built] | Pi cycle `mig-qs` (§6.4) to see the counter on hardware (`build.sh` now links `gamedrm/gamedrm_hooks.c` + `-Wl,--wrap=SDL_GL_SwapWindow`, as quake2-drm). Vsync-bound (poll-wake Finding 2: one flip in flight ⇒ 30–35 fps) |
| **Quake II** | `/usr/bin/quake2` (ram-stage launcher) → `/usr/bin/yquake2` (ref_gl3 = GLES3) | `quake2-drm` → `yquake2-drm` (**this pass**, §2.1) | 🟡 [built] | Pi cycle `mig-q2` (§6.1); SDL audio on `/dev/audio0` (KNOWN-ISSUES C5) now through the sdl2-drm audio driver |
| **Quake III** | `/usr/bin/quake3` → `/usr/bin/quake3e` (opengl1, QVM JIT) | `quake3-drm` → `quake3e-drm` (**this pass**, §2.2) | 🟡 [built] | Pi cycle `mig-q3` (§6.2) |
| **vkQuake** | `/usr/bin/vkquake`: SDL fully shimmed, no WSI — renders into a LINEAR VkImage mapped on `/dev/fb0` (`pl_phoenix_vk_vid.c`), old v3dv fork via `libv3dv-phoenix.a` | `vkq-drm` → `vkquake-drm` (**this pass**, §2.3): upstream vkQuake + SDL KMSDRM Vulkan (`VK_KHR_display`) + Mesa v3dv via phxvk | 🟡 [built] | Pi cycle `mig-vkq` (§6.3) incl. the #67 torch ROI check; FIFO-only present ⇒ ≤ 60 fps (old lane showed 73 on screen, no vsync) |
| **SuperTuxKart** | `/bin/stk` → `/usr/bin/supertuxkart` | `stk-drm` → `supertuxkart-drm` | ✅ [Pi] 11.89 fps = Pi OS parity (old lane 8.3) | exit-time EL0 fault root-caused to libphoenix `fclose(stdout)` UAF — fix on branch `fix/stdstream-fclose-uaf` (M3 "stk-drm exit fault"), check `stkdrm-2` |
| **X desktop** (`startx_gpu action`) | `/bin/startx_gpu` = `pl_phoenix_xlaunch`: starts `/sbin/rpi4-v3d` (old GPU daemon), `Xphoenix-glamor-daemon` (kdrive fbdev DDX + glamor shim, damage bands `glReadPixels`'d to `/dev/fb0`), then Window Maker + `gl-x11-window-daemon` + 2 × xterm (Life in CPython, top) + xbill + xclock | `Xorg-drm` / `Xorg-drm-m4p2` (xorg-server 21.1.24 hw/xfree86 + modesetting + glamor on GBM/EGL + DRI3/Present + `phxhid` input) | 🟡 [Pi] xclock (m4c), Window Maker desktop (m4d), DRI3/Present GL client **485 fps / 60.00 vsynced** (m4p2a) | (a) ✅ [built] **`startx-drm action`** (`xorg-drm/pi/startx-drm`, §6.6): Xorg-drm-noshim :1 + wmaker + the xlaunch `action` clients, `HOLD` + clean teardown; (b) ✅ [built] the GL window is `eglx11-demo` (DRI3/Present) with USPosition/USSize WM hints + title knob (`x11-drm/build-out-x`, staged `/bin/eglx11-demo-x`), placed where the old GL window is on screen; (c) `phxhid` checked statically against the kdrive driver (§6.6, identical protocol), never exercised on the Pi; (d) `-C` console handover untested — both in `mig-x` / `mig-x-input` (§6.6). **Not needed:** page flips of client buffers (G7; Present falls back to a copy), G4, G6 |
| X clients (wmaker, xterm, xclock, xbill, xcalc, dillo, …) | plain X11 clients of `Xphoenix` | the same binaries on `Xorg-drm` (m4c ran the old-lane `xclock`, m4d the old-lane `wmaker`) | ✅ [Pi] | none — no GPU code in them |
| **HEVC player** `/bin/hevc-play` | rpivid decode → `write()` to `/dev/fb0` | — | ⬜ | port to a KMS dumb buffer + plane (`drmModeAddFB` + atomic/`SETCRTC`), or keep on fbdev emulation (§4). Zero-copy of decoder frames needs **G7** (implemented, pending Pi `m6h-g7`; kms import of a foreign buffer) |
| `/sbin/rpi4-sysinfo` | lists `/dev/fb0` among the device nodes it reports | — | ⬜ | report `/dev/dri/card0`, `/dev/dri/renderD128`, `/dev/kms`, `/dev/v3d-async` instead (text change) |
| `/bin/fbprobe` (diagnostic) | writes test bytes to `/dev/fb0` | — | ⬜ | retire with `rpi4-fb`, or keep on fbdev emulation |
| **fbcon** (pl011-tty + teken) | draws the plo firmware framebuffer directly (not a `/dev/fb0` client) | unchanged | ✅ | stays. rpi4-kms planes stack above it (M2 §10); `-C` hands the console over (`FBCONSETMODE`), `-B` blanks the fb layer |
| Weston (M6) | — | `weston-drm` | 🟡 [Pi] composites wl_shm + GL clients (m6c); SIGTERM exit open | **not required for the migration** — no shipped Wayland user |
| New-lane test tools | — | `kmscube`, `vkcube-drm`, `drmprobe`, `kmstest`, `eglx11-demo`, `v3dasync-ping` | ✅ [Pi] | ship as the lane's smoke tests |

No other program on the image links Mesa, SDL or touches the V3D. `Xphoenix` (plain, non-glamor kdrive)
is also an `/dev/fb0` writer and goes with the kdrive DDX.

## 2. The three new clones (this pass) [built]

All three live in `tools/gpu-lane/sdl2-drm/` and write only `build-out/<app>-drm/` (gitignored). None
runs `sdl2-drm/build.sh`, `mesa-drm/build.sh` or `vulkan-drm/build.sh`; each checks that the shared inputs
it reads are unchanged afterwards (the default `libSDL2.a` `4abf34e0…`, `quakespasm-drm` `ac29ad23…`,
`supertuxkart-drm` `567f12b5…`, `vkcube-drm`, the ICD, libdrm m5b, the shipped binaries — all verified
unchanged). `quakespasm-drm` has since been relinked with the frame counter (§6.4: `e6335955…`); the
scripts snapshot their guarded inputs per run, so they compare against whatever is current.

**Current default inputs (adoption rebuild, 2026-09-27 ~15:50, after build 17):** SDL patch set
`d145b0e6…` = `patches/0001–0009` + overlay (0009 = the frame-pacing reorder), `libSDL2.a` **`7a1de5d1…`**
(the Vulkan variant `1de303a9…`, set `cd1e07eb…`); Mesa-GL rebuilt with mesa-drm's committed 16-patch set
`4a457a1e…` (was `278cdef4…`, 12 patches: 0013/0014 are X11-platform build fixes, 0015 a meson stub, 0016
sets a low-memory placement flag only for scan-out resources allocated on the *render* device — kmsro
clients allocate theirs through renderonly on card0, so it is not reached by these games [read
`v3d_resource.c`]); vkQuake patch set `46e27a38…` = `patches-vkquake/0001–0007`; libphoenix.a
`2acb195e…`. Every clone's own checks pass and all three control relinks are **byte-identical to build 17's
shipped engines**. New proof in every script: `gamedrm/check-swap-order.sh` (objdump of
`KMSDRM_GLES_SwapWindow`: the first `bl KMSDRM_WaitPageflip` must come after `bl KMSDRM_FBFromBO`;
stock SDL waits first) — `submit-first` for all five ELFs.

```
tools/gpu-lane/sdl2-drm/build-quake2-drm.sh    # ~1 min: control relink + clone relink + proofs + launcher
tools/gpu-lane/sdl2-drm/build-quake3-drm.sh    # ~1 min
tools/gpu-lane/sdl2-drm/build-vkquake-drm.sh   # ~2 min cold (SDL-Vulkan variant + 83 TUs + link)
```

### 2.1 `quake2-drm` / `yquake2-drm`

* **How:** the yquake2 port's own final link, taken verbatim from its `build.log` (the ports framework
  traces `p_build` with `set -x`), re-run with the two old SDL-GL glue objects (`sdl_phoenix_glctx.o`,
  `sdl_phoenix_glstubs.o`) dropped and the old group (ports `libSDL2.a`, `libGL-phoenix.a`,
  `libv3d-phoenix.a`) replaced by the new stack **in the stk-drm shape**: yQuake2's ref_gl3 is a GLES3
  renderer that loads every `gl*` through glad + `SDL_GL_GetProcAddress`, so libgallium (whole-archive) +
  one group of the KMSDRM `libSDL2.a`, EGL/GBM/dri_gbm/**libGLESv2**/shared-glapi/v3d/broadcom/winsys/util,
  libdrm-phoenix m5b, the compat shim, zlib; `-static -Wl,--wrap=mmap,--wrap=ioctl,--wrap=SDL_GL_SwapWindow`.
  The 137 engine objects, their order, the port's flags, `-lstdc++ -lm` and the 4 MiB main stack are the
  port's. Shared body: `gamedrm/relink-sdl-gl-game.sh`.
* **Control relink** (the port's command with only `-o` changed): **byte-identical to the shipped
  `prog/yquake2`** (`e36dd0ed…`, rebuilt 11:54) — the clone's engine objects are exactly the shipped ones.
* **Frame counter:** `gamedrm/gamedrm_hooks.c` (new, BSD-3; the stk-drm hooks with the name as a
  parameter): banner, SDL VIDEO/INPUT at DEBUG, and `quake2-drm flipstat N frames in T ms = X fps
  (total M)` every 5 s + `swapstat` — the shape `run-showcase-gate.sh` reads for `frames` (neither engine
  prints fps outside `timedemo`). objdump: `GL3_SwapWindow → __wrap_SDL_GL_SwapWindow → SDL_GL_SwapWindow`.
* **Launcher:** `tools/yquake2-port/quake2-launcher.c` with exactly one line changed (exec target
  `/usr/bin/yquake2-drm`): the same `ram-stage-play /usr/share/quake2 /tmp/quake2`, `+set vid_renderer gl1`
  (inert on the single-ELF gl3 binary, on both lanes), 1920×1080 custom mode, `vid_fullscreen 2`,
  `+map demo1`. Installed as `/usr/bin/quake2-drm`.
* **Checks:** `nm -u` = 0, no PT_INTERP; KMSDRM / SDL EGL / Phoenix HID / GBM / kmsro / v3d DRM winsys /
  libdrm-phoenix incl. `__wrap_ioctl` present; old-lane symbols and strings (`v3d-winsys:`, `phxgl`,
  `/dev/fb0`, `RPI4FB_GETMODE`, `peek_next_scanout`, `v3d-srv`, …) **0**, and the shipped `yquake2` the
  reverse (inverse control); real `ioctl`/`mmap` called only from the wrappers; **no global symbol defined
  by both the engine's objects and the new stack**. `pthread_getcpuclockid` (all `sdl_phoenix_glstubs`
  provided) is not needed by the new link.
* **Outputs:** `yquake2-drm.stripped` 18 498 688 B (`33fb96f1…`; shipped 19 139 688 B), unstripped
  `yquake2-drm` + `.map` for addr2line, `quake2-drm` (`b08ee6a4…`), `BUILD-INFO.txt`. **Adoption rebuild:**
  `yquake2-drm.stripped` 18 499 104 B **`b39f49cf6e2c4427…`** (with patches/0009), launcher unchanged
  `b08ee6a4…`.

### 2.2 `quake3-drm` / `quake3e-drm`

* **How:** the same, from the quake3 port's link (167 engine objects), in the **quakespasm-drm shape**:
  quake3e's opengl1 renderer is a desktop (compatibility) GL program, so `libglapi_bridge.a` replaces
  libGLESv2. quake3e resolves every `qgl*` through `SDL_GL_GetProcAddress`; the map shows **no** bridge
  member pulled (no link-time `gl*`), so the bridge is inert — what matters is that SDL asks EGL for a
  desktop GL context on the `opengl=true` Mesa, as quakespasm-drm does.
* **Control relink:** **byte-identical to the shipped `prog/quake3e`** (`39bca8d7…`, rebuilt 11:54).
* **Launcher:** `tools/quake3-port/quake3-launcher.c`, one line changed (`/usr/bin/quake3e-drm`): same
  ram-staging to `/tmp/quake3`, `fs_basepath`/`fs_game demoq3`; the gate appends `+map q3dm1`.
* **Checks:** as 2.1 (`GLimp_EndFrame → __wrap_SDL_GL_SwapWindow`). The QVM JIT's `mmap(PROT_EXEC)` now
  passes through libdrm-phoenix's `__wrap_mmap` (a pass-through for non-DRM descriptors) — a new caller of
  the wrapper, pre-registered in §6.2.
* **Outputs:** `quake3e-drm.stripped` 18 570 288 B (`6d69a9db…`; shipped 19 209 920 B), `quake3-drm`
  (`0f1045c2…`). **Adoption rebuild:** `quake3e-drm.stripped` 18 570 704 B **`5fab2b12058d84f1…`**,
  launcher unchanged `0f1045c2…`.

### 2.3 `vkq-drm` / `vkquake-drm`

**Not a relink** — there is nothing to relink: the port replaces every SDL/platform TU with Phoenix glue
whose video half is a no-WSI `/dev/fb0` shim, and carries engine hunks that exist only for that shim. So
**no byte-identical control is possible**; the inverse control is that the shipped `vkquake` carries the
old strings (`/dev/fb0`, `phoenix-map.cfg`) and none of the new ones.

* **Source:** upstream vkQuake at the port's pinned commit (`1aa13a56…`, from the port's tarball) with
  **upstream's TU list** (meson `srcs` + the non-Windows block: `gl_vidsdl.c`, `in_sdl*.c`, `snd_sdl*.c`,
  `main_sdl.c`, `sys_sdl_unix.c`, `pl_linux.c`, `net_bsd.c`, …, 83 TUs, 0 warnings) and four patches taken
  unchanged from the port, the ones that do not concern video (`tools/gpu-lane/sdl2-drm/patches-vkquake/`):
  0001 `cmdline` published on shareware too (so `+map start` works), 0002 `SV_LocalSound` NULL-client
  guard, 0003 slurp-and-close file reads (NFS + libphoenix's concurrent-stream limit), 0004 the #29
  texture-copy extent re-derivation (`__phoenix__`; a no-op when the extents are right — kept for the
  first cycle, candidate for removal after one A/B). Since the `perf-vkq-b` adoption also **0006**
  (`r_oit` defaults to 0 on Phoenix: no WBOIT render passes) and **0007** (RGBA8 scene colour buffer on
  vendor 0x14E4 instead of A2B10G10R10, which V3D tiles at 16F) — [vkquake-perf.md](vkquake-perf.md). Plus one **new** patch, 0005: no timestamp query
  pool on Phoenix — vkQuake records `vkCmdResetQueryPool` + 2 × `vkCmdWriteTimestamp` every frame, v3dv
  runs both as CPU jobs through `DRM_IOCTL_V3D_SUBMIT_CPU` (the server advertises the CPU queue, which v3dv
  requires, but answers `SUBMIT_CPU` with `-ENOSYS` — gap **G5**), so the first frame would lose the
  device [read `v3dv_queue.c` `handle_reset_query_cpu_job`, `v3da_main.c`]; the pool only feeds
  `scr_speeds`' GPU time. Remove 0005 when the server serves `SUBMIT_CPU`. **Dropped** (fb0-shim only): `PL_VkHostAllocator`
  for shader modules, the de-static'd pipeline helpers, 2D `CULL_MODE_NONE`, `SCR_DrawGUI` canvas removal,
  the demo-loop arming, the alias alpha=1 hunks (the display plane is XRGB8888: alpha is ignored). SPIR-V:
  the port's vendored `glue/vkquake_shaders.c` (this commit's shaders; the alias-alpha shader hunk tests a
  ubo flag bit only the dropped `r_alias.c` hunk sets, so it is inert).
* **SDL:** a second build of the sdl2-drm SDL tree (same patches 0001–0009 + overlay) with
  `SDL_VULKAN=ON` and `patches-sdl-vulkan/0001` (+ PHOENIX in the `SDL_VULKAN` option's condition,
  `SDL_VIDEO_VULKAN` in the Phoenix video block, and the `SDL_vulkan_internal.h` "no dummy loadso" gate
  lifted for `__phoenix__`). SDL's **stock** `SDL_kmsdrmvulkan.c` then provides the instance extensions
  (`VK_KHR_surface` + `VK_KHR_display`) and `SDL_Vulkan_CreateSurface` (`vkCreateDisplayPlaneSurfaceKHR`
  on the mode matching the window size). The default sdl2-drm `libSDL2.a` stays `SDL_VULKAN=OFF` and
  byte-identical.
* **No loader, no dlopen:** SDL loads Vulkan with `SDL_LoadObject("libvulkan.so.1")` +
  `SDL_LoadFunction("vkGetInstanceProcAddr")`. The binary is linked
  `-Wl,--wrap=SDL_LoadObject,--wrap=SDL_LoadFunction,--wrap=SDL_UnloadObject`; `vkqdrm/vkqdrm_hooks.c`
  answers those two with a sentinel handle and `vkqdrm_GetInstanceProcAddr` = **phxvk** (the vkcube-drm
  loader stand-in in front of Mesa's `vk_icdGetInstanceProcAddr`); anything else goes to SDL's own.
  objdump: `KMSDRM_Vulkan_LoadLibrary → __wrap_SDL_LoadObject / __wrap_SDL_LoadFunction`.
* **Trampolines:** vkQuake calls 75 core `vk*` commands as link symbols (79 before 0005 made the query
  calls dead code). `vkqdrm/gen-vk-trampolines.py`
  emits them from the objects' own undefined `vk*` symbols and `vulkan_core.h`'s prototypes: global
  commands resolve with a NULL instance, `vkCreateInstance` records its instance, everything else resolves
  against it — for device commands Mesa's `vk_instance_get_proc_addr()` returns its `vk_device_trampolines`
  entry, which dispatches through the object's own table [read `vk_instance.c:340-365`], so no `VkDevice`
  capture is needed (the port's trampolines needed `g_vk_device` published by the fb0 shim).
* **No Mesa GL in the binary:** SDL's KMSDRM driver binds GBM + EGL statically (sdl2-drm patch 0006) for
  GL windows. vkQuake creates only an `SDL_WINDOW_VULKAN` window, which takes KMSDRM's GBM/EGL-free branch
  [read `SDL_kmsdrmvideo.c:1530-1608`]. Linking the GL Mesa build as well would put a second copy of
  Mesa's util/NIR/broadcom compiler next to the ICD's, so the 44 `gbm_*`/`egl*` symbols the SDL archive
  references are generated stubs that print `vkquake-drm: GL path not linked in this binary, called: <fn>`
  and fail. Checked: `gbmint_get_backend`, `kmsro_drm_screen_create`, `_mesa_glapi_get_proc_address`
  absent.
* **Frame counter:** the present is wrapped where the engine fetches it (`vkGetDeviceProcAddr`), on top of
  phxvk's own `phxvk: run presents=… fps=…`: `vkquake-drm flipstat N frames in T ms = X fps (total M)` +
  `presentstat` every 5 s (vkQuake's own fps is on-screen only).
* **Two libphoenix gaps, bridged for these TUs only:** `vkqdrm/vkqdrm_compat.h` (`<arm_neon.h>`, which
  upstream gets from its PCH; `struct ipv6_mreq`, as the port's compat header) and
  `vkqdrm/include/execinfo.h` (zero-frame `backtrace()` for `Sys_StackTrace`). The real fixes are on
  libphoenix branch `feat/ipv6mreq-execinfo` (§3); both bridges step aside by themselves once the
  sysroot has them (`#ifndef IPV6_ADD_MEMBERSHIP`; `-idirafter`), and are deleted after the merge.
* **Link:** vkcube-drm's shape (C++ driver, `-static`, `--gc-sections`, 4 KiB pages, the ICD
  whole-archive, `--wrap=mmap/ioctl`) + the loadso wraps + the port's 32 MiB main stack. Link log empty.
* **Launcher `vkq-drm`** (`vkqdrm/vkq-drm-launcher.c`): upstream `main_sdl.c` takes argv, so the port's
  hard-wired start-up becomes a command line: `/usr/bin/vkquake-drm -basedir /usr/share/quake -width 1920
  -height 1080 -fullscreen +r_rtshadows 0 +map start` (the same data dir, over NFS as the gate ran it; the
  1080p mode — KMSDRM's Vulkan surface must match an existing display mode; `r_rtshadows 0` as the port
  forced; `map start`, the torch-check viewpoint; `r_gpulightmapupdate` is 1 by default upstream).
  `id1/phoenix-map.cfg` / `phoenix-demo.cfg` do not apply to this binary.
* **Checks:** `nm -u` = 0, no PT_INTERP; KMSDRM Vulkan (`KMSDRM_Vulkan_LoadLibrary/CreateSurface/
  GetInstanceExtensions`), phxvk, `vk_icdGetInstanceProcAddr`, `v3dv_*`, `wsi_CreateDisplayPlaneSurfaceKHR`,
  `wsi_CreateSwapchainKHR`, `wsi_QueuePresentKHR`, `drmModeAtomicCommit`, libdrm-phoenix present; strings
  `VK_KHR_display`, `V3D %d.%d.%d.%d`, `phxvk: new GPU lane`, `vkquake-drm: new GPU lane`, `KMS/DRM Video
  Driver`; old-lane strings (`/dev/fb0`, `pl_phoenix`, `PL_VkHostAllocator`, `vkvid:`, `phoenix-map.cfg`,
  `vktramp:`, `V3DV_PHOENIX`, `v3d-winsys:`) **0**; real `ioctl`/`mmap` only from the wrappers.
* **Outputs:** `vkquake-drm.stripped` 13 365 608 B (`20e3d43f…`; shipped `vkquake` 13 123 824 B),
  `vkq-drm` (`aaf70271…`), `vk-direct-calls.txt`, `gl-stub-names.txt`, `BUILD-INFO.txt`. **Adoption
  rebuild** (0001–0007, SDL with 0009): `vkquake-drm.stripped` 13 368 776 B **`22755bb450b09e0f…`**,
  `vkq-drm` **`e49a7444fc782d0f…`** (execs `/usr/bin/vkquake-drm`); gdb on the unstripped ELF:
  `r_oit.string = "0"`; string `Using R8G8B8A8 color buffer format (V3D: …)` present.

## 3. Remaining blockers, by user

| Blocker | Affects | State | Needed for migration? |
|---|---|---|---|
| Pi cycles of the three clones | q2, q3, vkq | pre-registered §6 | **yes** |
| `flipstat` in quakespasm-drm | the gate's `frames` column | ✅ [built] 2026-09-27: `sdl2-drm/build.sh` links the shared `gamedrm/gamedrm_hooks.c` (`-DGAMEDRM_NAME='"quakespasm-drm"' -DGAMEDRM_API='"desktop GL"'`, replacing `qsdrm/qsdrm_banner.c`, whose banner and SDL log levels it reproduces byte for byte) and `-Wl,--wrap=SDL_GL_SwapWindow`; objdump `GL_EndRendering → b __wrap_SDL_GL_SwapWindow → bl SDL_GL_SwapWindow`, one direct call of the real swap (the wrapper's). Pi check: `mig-qs` (§6.4) | **yes** (else the gate fails mechanically) — done pending `mig-qs` |
| libphoenix gaps `struct ipv6_mreq` + `<execinfo.h>` | vkquake-drm (bridged in `vkqdrm/`), and the yquake2/quake3/vkquake ports (own `ipv6_mreq` copies) | ✅ [built] on branches `feat/ipv6mreq-execinfo` (pushed to `publish`, **not merged**): libphoenix `17c4fae` (`ipv6_mreq` + `IPV6_ADD/DROP_MEMBERSHIP`; `IPV6_JOIN_GROUP`/`LEAVE_GROUP`/`V6ONLY` renumbered to lwip's 12/13/27 — lwip receives optname unchanged) + `62e76b8` (`backtrace()` = aarch64 frame-record walk, 0 frames elsewhere; `backtrace_symbols[_fd]` = `0x<hex>`, one block); phoenix-rtos-tests `a8f2d6b` (`test-libc-execinfo`, `misc/netinet_in.c`); phoenix-rtos-ports `35abace` (the three ports' copies skip themselves when `IPV6_ADD_MEMBERSHIP` is defined — without it the libphoenix merge breaks their builds: a second `struct ipv6_mreq` is an error under gnu11/gnu17). On rpi4b lwip is built without IPv6, so `IPPROTO_IPV6` options stay ENOPROTOOPT whatever the number | no (the bridges work). **Merge order:** ports `35abace` first (or together), then libphoenix, then tests. **After the libphoenix merge, delete:** `tools/gpu-lane/sdl2-drm/vkqdrm/include/execinfo.h` and the `ipv6_mreq` block of `vkqdrm/vkqdrm_compat.h` (+ its `-idirafter` in `build-vkquake-drm.sh`), and the three ports' copies (`yquake2`/`quake3` `glue/pl_phoenix_compat.h`, `vkquake/glue/vkq_phoenix_compat.h`) |
| X `action` launcher on Xorg-drm + a GL window client | X desktop | ✅ [built] 2026-09-27: `tools/gpu-lane/xorg-drm/pi/startx-drm` (staged `/bin/startx-drm`) + `eglx11-demo` with WM placement hints (`/bin/eglx11-demo-x`); host dry-run with stub binaries PASS; **Pi cycle `mig-x` pre-registered (§6.6)** | **yes** — done pending `mig-x` |
| Shader disk cache in Mesa-DRM | every GL/Vulkan app: cold shader compiles at every start (STK loads at < 1 fps for a while) | not built | no (startup time only); wanted before shipping |
| libphoenix `fclose(stdout)` UAF fix | STK exit fault (old and new lane) | branch `fix/stdstream-fclose-uaf` | yes for a 0-fault gate (the fault is at exit, inside the capture) |
| **G4** render-node export (`V3DA_OP_BO_EXPORT` + `/v3dbuf`) | UIF client buffers in X, Wayland dmabuf, v3dv external memory | implemented 2026-09-27 (`52f039791`), pending Pi `m6g-g4` ([M6 §15](M6-wayland.md)) | no (DRI3 with `dmabuf_capable` off works — m4p2a) |
| **G6** cross-process implicit sync (+ `BO_LAST_FENCE`) | Weston composition and direct scan-out of GPU clients without tearing, Present flips of client buffers, Mesa WSI dma-buf sync | **implemented, pending Pi `g6-sync`** ([G6 doc](G6-cross-process-sync.md): render server proto 4, `DMA_BUF_IOCTL_EXPORT/IMPORT_SYNC_FILE`, foreign flips gated) | no (tearing risk only) |
| G6b cross-process syncobj / sync-file **descriptors** | DRI3 1.4 explicit sync, Vulkan **xcb** WSI, `wp_linux_drm_syncobj` | open (`/v3dsync`, proto 5) | no (no shipped Vulkan-in-X user) |
| **G7** kms import of a foreign buffer (+ `BO_LAST_FENCE`) | Present flips of client buffers, Weston direct scan-out, zero-copy HEVC | **G7 implemented, pending Pi `m6h-g7`** ([M6 §16](M6-wayland.md): `KMS_OP_PRIME_IMPORT`, LINEAR below 1 GiB); `BO_LAST_FENCE` = G6 (implemented, pending Pi `g6-sync`) | no (copy fallback) |
| **G5** `SUBMIT_CPU` | every Vulkan query (v3dv runs timestamp resets/writes, query copies and indirect CSD as CPU jobs) | open | sidestepped for vkQuake by patch 0005 (its only queries feed `scr_speeds`); **needed** before any Vulkan app that relies on queries ships |
| G12 | `poll()` quantum | ✅ closed (pollNotify, build 11) | — |
| G15 | sync_file ioctls | ✅ in-process (`--wrap=ioctl`, m5b) | — |
| G16 | DRI3 fences | ✅ xshmfence backend over `shmsrv` | — |
| Input | games: SDL KMSDRM + `SDL_PHOENIX_HID_Poll` (`/dev/kbd0`, `/dev/mouse0`); X: `phxhid` | built, not exercised on the Pi | no for the gate (no input); **yes** before the owner uses the desktop |
| Weston | Wayland | m6c displays; exit open | **no** |

## 4. What changes in the image build

1. **Mesa:** one `mesa-drm` framework port (Mesa 26.2.0 + `tools/gpu-lane/mesa-drm/patches/mesa/0001–0012`)
   replaces the Mesa fork (`external/mesa` phoenix branch, `sources/phoenix-rtos-devices/gpu/rpi4-v3d/mesa/
   build-{gl,v3d,v3dv}-phoenix.py`, `tools/.gpu-libs/lib{GL,v3d,v3dv}-phoenix.a`, `/tmp/mesa-v3d-build`).
   Static archives for: gallium v3d + GBM + EGL (GLES + desktop GL, `platforms=` none/x11/wayland as
   needed by the consumer) and the v3dv ICD (`--vulkan`). Today these are three build dirs; for a port,
   either keep them as sub-builds or build one tree with `-Dgallium-drivers=v3d,kmsro
   -Dvulkan-drivers=broadcom` (a static program must still link only one of GL or Vulkan unless the shared
   internal libraries are split out — vkquake-drm's GL stubs are the workaround for SDL's side).
2. **libdrm-phoenix** becomes a port (today `tools/gpu-lane/libdrm-phoenix`, `build-out-m5b`): `libdrm.a`
   + headers; every consumer links with `-Wl,--wrap=mmap -Wl,--wrap=ioctl`.
3. **ports/sdl2** switches to KMSDRM: the sdl2-drm build (SDL 2.30.12 + `tools/gpu-lane/sdl2-drm/patches/
   0001–0009` + overlay: Phoenix audio + HID; 0009 = the frame-pacing swap reorder), `SDL_VULKAN=ON` with `patches-sdl-vulkan/0001` (costs
   nothing for GL users: the Vulkan code needs no link dependency). The `/dev/fb0` video backend
   (`SDL_phoenixvideo.c`, `PHOENIX_*`), `sdl2/glue/sdl_phoenix_glctx.c` and `sdl_phoenix_glstubs.c` are
   deleted.
4. **Game ports** (quakespasm, yquake2, quake3, supertuxkart): link the new stack instead of
   `libSDL2.a(old) + libGL-phoenix + libv3d-phoenix + glue` — exactly the `gamedrm/relink-sdl-gl-game.sh` /
   `build-stk-drm.sh` substitution, moved into each `p_build` (GLES shape for yquake2/STK, desktop-GL
   bridge shape for quakespasm/quake3). The `external/mesa/include` include path becomes the mesa-drm
   headers. Their engine patches stay.
5. **ports/vkquake** is rewritten: upstream TU list, patches-vkquake 0001–0007 instead of the fb0 patch (0005
   only until G5; 0006/0007 the V3D performance defaults),
   SDL2 dependency (`depends="sdl2"`), the v3dv ICD + phxvk + generated trampolines + the loadso wraps;
   `glue/` shrinks to the SPIR-V (or regenerate it with the host glslang) — `pl_phoenix_*` and
   `vk_trampolines.c` are deleted.
6. **X:** an `xorg_server_drm` port (`tools/gpu-lane/xorg-drm`: Xorg-drm with modesetting + glamor +
   `phxhid`, builtin-module table, xshmfence backend) replaces `xorg_server`'s kdrive `Xphoenix` /
   `Xphoenix-glamor-daemon`; `startx`/`startx_gpu` (`pl_phoenix_xlaunch`) become one launcher for
   Xorg-drm (same client layouts). The GL-in-X demo client is rebuilt on Mesa-DRM `--x11` (EGL via
   DRI3/Present).
7. **Servers** move to `sources/phoenix-rtos-devices` (`gpu/rpi4-v3d-async`, `video/rpi4-kms`, and
   `shmsrv`) and are **started at boot** from `user.plo.yaml` (both nfsroot and sd blocks), in place of
   `rpi4-fb` — order: after `rpi4-vcmbox` (both use the mailbox) and before `psh`:
   `rpi4-v3d-async -r 1 -m serial -i` (render; `-i` = IRQ-driven completion as in every cycle),
   `rpi4-kms -G` (display; `-G` = render-fence-gated flips, G13 — the `rpi4-kms-gate` build: deferred-flip
   wake fix + `pollNotify`), `shmsrv` (`/shm`, xshmfence pages for DRI3, Weston wl_shm). ⚠ plo rule: a
   program name may appear only once as an `app -x` alias; verify the boot after the edit.
8. **Device names:** `/dev/dri/card0` + `/dev/kms` (rpi4-kms, one port), `/kmsbuf/<id>` (dumb-buffer
   exports), `/dev/dri/renderD128` + `/dev/v3d-async` (one port) and `/dev/dri/card1` (rpi4-v3d-async),
   `/shm` (shmsrv). `/dev/v3d-srv` (old daemon) and `/dev/fb0` (rpi4-fb) disappear — unless fbdev
   emulation is kept (item 9).
9. **`/dev/fb0`:** after the migration its only users are `hevc-play`, `fbprobe` and `rpi4-sysinfo`'s
   listing (§1). Either port `hevc-play` to a KMS dumb buffer and drop `/dev/fb0`, or implement M2 §10
   item 4 (rpi4-kms registers `fb0` itself — `read`/`write` + `RPI4FB_GETMODE` on a pool BO shown on the
   primary plane while no KMS client is active). Recommended: port `hevc-play` (one program), no emulation.
10. **Kernel:** nothing new for the migration — the new lane uses facilities already on master
    (`memExport`/object export from E1, `pollNotify` from build 11, the port-death fix); confirm with the
    migration manifest that the image carries them.
11. **Boot config:** `config.txt` keeps `dtoverlay=vc4-fkms-v3d` (firmware framebuffer for fbcon) and
    `core_freq=500`. `gpu_mem=128` was sized for plo's triple-height firmware fb for the old lane's
    pan-flip present (3 × 1080p); with rpi4-kms planes the firmware fb only carries fbcon, so it can shrink
    (research doc §migration: "kept only for boot splash/fbcon until M2, then reduced") — a separate,
    measured change after the migration gate.

### 4.1 Ports (graphics) — the new lane stored in phoenix-rtos-ports [built]

Owner request 2026-09-27: "make sure that all the ports (the recent ones) are correctly stored in
phoenix-rtos-ports". The graphics half of the new lane (this section; the Wayland-desktop half —
labwc, gtk3, dbus, xfce — is branch `feat/new-lane-wayland-ports`) is now a set of framework recipes on
phoenix-rtos-ports branch **`feat/new-lane-graphics-ports`** (`78d3428`, 7 commits off `35abace`, pushed to
`publish`, **not merged**). They
are **opt-in**: no project `ports.yaml` names them, so the default image is unchanged (items 1–6 above
are what adopting them means). The old-lane ports (`sdl2`, the four game ports, `xorg_server`, …) are
untouched and build exactly as before.

| Package | Port | From `tools/gpu-lane/` | Licence | Notes |
|---|---|---|---|---|
| libdrm 2.4.134-16-gb97cbde + Phoenix backend | `libdrm_phoenix` 2.4.134 | `libdrm-phoenix` (+ `v3d-async/v3da_proto.h`, `kms/kms_proto.h`) | MIT AND BSD-3-Clause | patches 0001–0004; backend + wire headers vendored in `glue/phoenix/`; `drmprobe`; installs `share/phoenix-newlane/newlane.subr` (the lane's build helpers) |
| Mesa 26.2.0 | `mesa_drm` 26.2.0 | `mesa-drm` (+ `vulkan-drm/phxvk`) | MIT | patches 0001–0016, `glue/compat`; always the GLES build, USE `opengl` / `wayland` / `x11` / `vulkan` add the tools' `mesa-gl` / `build-out-wayland` / `build-out-x11` / `build-out-vulkan` builds, each with its own prefix + `link-*.txt`; `vulkan/phxvk/` |
| kmscube f60e50e | `kmscube_drm` 0.0.1 | `mesa-drm` (kmscube part) | MIT | |
| SDL 2.30.12 KMSDRM | `sdl2_kmsdrm` 2.30.12 | `sdl2-drm` (build.sh steps 2–3) | Zlib | patches 0001–0009 (0009 = frame pacing), overlay (Phoenix HID + audio); USE `vulkan` = the SDL_VULKAN=ON variant (`patches/vulkan/0001`); installs `share/gamedrm/` (hooks, `check-swap-order.sh`, `relink-sdl-gl-game.subr`) |
| quakespasm-drm | `quakespasm_drm` 0.97.0 | `sdl2-drm` (build.sh step 4) | GPL-2.0-or-later | compiled from source; the quakespasm port's patch + glue vendored |
| yquake2-drm + quake2-drm | `yquake2_drm` 8.71 | `sdl2-drm/build-quake2-drm.sh` | GPL-2.0-or-later | RELINK of the `yquake2` port's objects (a `depends`), control relink byte-identity kept |
| quake3e-drm + quake3-drm | `quake3_drm` 1.32 | `sdl2-drm/build-quake3-drm.sh` | GPL-2.0-or-later | relink of the `quake3` port's objects |
| supertuxkart-drm + stk-drm | `supertuxkart_drm` 1.4 | `sdl2-drm/build-stk-drm.sh` | GPL-3.0-or-later | relink of the `supertuxkart` port's CMake build |
| vkquake-drm + vkq-drm | `vkquake_drm` 1.34 | `sdl2-drm/build-vkquake-drm.sh` | GPL-2.0-or-later | upstream TU list, patches-vkquake 0001–0007, `glue/vkqdrm/`; SPIR-V read from the `vkquake` port's `glue/` |
| vkcube-drm | `vkcube_drm` 1.4.350 | `vulkan-drm` | Apache-2.0 | Vulkan-Tools vulkan-sdk-1.4.350.0 + patch 0001 |
| libwayland 1.24.0, wayland-protocols 1.45, wlphx-compat, shmsrv | `wayland` 1.24.0 | `weston-drm` | MIT AND BSD-3-Clause | shmsrv folded in (a port needs an upstream archive); installs `compat/include`, `mesa-compat/include`, `deps/libffi` |
| Weston 14.0.2 (+ xkbcommon 1.7.0, display-info 0.2.0, seatd 0.9.1, libinput 1.26.2 header, shims) | `weston` 14.0.2 | `weston-drm` | MIT AND BSD-3-Clause AND BSD-2-Clause | patches weston 0001–0008, seatd 0001–0004 |
| libxshmfence 1.3.2 Phoenix backend (G16) | `libxshmfence_phoenix` 1.3.2 | `x11-drm` | MIT | |
| libepoxy 1.5.10 (static EGL) | `libepoxy` 1.5.10 | `xorg-drm` | MIT | a port of its own (the Wayland half's gtk3 builds its own copy: merge overlap) |
| Xorg-drm (xorg-server 21.1.24, modesetting + glamor, phxhid) + libxcvt 0.1.2 + eglx11-demo | `xorg_server_drm` 21.1.24 | `xorg-drm`, `x11-drm` | MIT AND BSD-3-Clause | the `build-out-noshim` configuration; USE `x11demo` = eglx11-demo (pulls `mesa_drm[x11]`); `startx-drm`, `xorg-drm.conf` |

**Conventions (all new-lane ports).** (a) A **private install prefix**: `conflicts="<name>!=<version>"`
makes port_manager install into `versioned-ports/<name>-<version>/` instead of the shared prefix the old
lane compiles and links from (a conflict with the old-lane counterpart, e.g. `sdl2`, would make the
relink clones unresolvable). (b) **No framework CFLAGS**: they carry `-I<prefix>/include`, which holds the
old lane's GL/X11 headers; `newlane.subr` gives every recipe the tools scripts' exact flag set, the E7
`-pthread`-dropping wrappers, meson cross files and private dependency views. (c) **USE `rootfs`**: binaries
always go to `<prefix>/bin` (+ unstripped `prog/`), into the image's rootfs only with USE `rootfs` — so
`scripts/build-port.sh` of a new-lane port changes nothing the image picks up. (d) Every patch/glue file
is a **copy** of the `tools/gpu-lane` file; `scripts/check-gpu-lane-ports-sync.sh [<ports dir>]` compares
all of them (**160 files, 45 mappings: identical**). The `tools/gpu-lane/*/build.sh` headers point at their
ports. Dependency mapping `mesa_drm[opengl]`, `sdl2_kmsdrm[vulkan]` etc. is resolved by port_manager (USE
propagation); `--dry build` of all 15 ports with `rootfs` resolves (the dependency closure is correct,
incl. `mesa_drm +opengl +vulkan +wayland +x11`).

**libdrm-phoenix: decision.** A port now (upstream libdrm + our patches; the Phoenix backend and the two
server wire headers vendored in `libdrm_phoenix/glue/phoenix/`). It is our code and speaks the servers'
protocols, so it moves with the servers (item 7: `gpu/rpi4-v3d-async`, `video/rpi4-kms` → phoenix-rtos-devices):
then the wire headers come from devices and the backend can move to phoenix-rtos-corelibs, leaving the
port as plain upstream libdrm + patches. Until then the vendored copies are held identical by the sync check.

**Verification** (scratch buildroot `scripts/make-scratch-buildroot.sh`, never the image's `.buildroot`;
`RPI4B_BUILDROOT=<scratch> RPI4B_PORTS_DIR=<ports worktree> scripts/build-port.sh <port>`; outputs compared
with `scripts/gpu-lane-compare.sh` against the tools builds). Verdict scale: IDENTICAL (sha256) > CODE-IDENTICAL
(equal after `strip --strip-debug`, or disassembly + relocations equal) > CODE-EQUIVALENT (the same with
string-literal offsets / literal addresses masked; the remaining string differences listed are the
`__FILE__` build paths and the build timestamp only).

| Port | How verified | Result |
|---|---|---|
| `libdrm_phoenix` | **framework build** (scratch) | `libdrm.a` CODE-IDENTICAL, headers identical; `drmprobe` CODE-EQUIVALENT (1 path string) |
| `mesa_drm` | **framework build**, GLES alone and again with USE `opengl`+`vulkan` (pulled by the consumers below) | vs the tools `mesa-gl` (same 16-patch set `4a457a1e…`): `libGLESv2.a`, `libmesadrm-compat.a` CODE-IDENTICAL; `libEGL.a`, `libgbm.a`, `libgallium-26.2.0.a` CODE-EQUIVALENT (paths + the `(git-9f0a761020)` suffix: a tarball build has no git). The tools `build-out` (GLES) and `build-out-vulkan` are older patch sets (`d71e7ff7…`, `60dd139d…`, 11 patches) → the ICD differs by exactly those patches, not comparable |
| `sdl2_kmsdrm` (+ USE `vulkan`) | **framework build** + recipe harness | both `libSDL2.a` CODE-EQUIVALENT (paths), headers identical |
| `quakespasm_drm` | **framework build** + harness against the tools inputs | harness: **CODE-IDENTICAL** to `sdl2-drm/build-out/quakespasm-drm.stripped` (2 strings: build time) — after one fix found this way: `link-gl.txt` must put `libglapi_bridge.a` first, as the tools link does (member order decides layout) |
| `yquake2_drm`, `quake3_drm`, `supertuxkart_drm` | recipe harness (the real `p_prepare`/`p_build`, framework env, deps = the tools-lane Mesa/SDL/libdrm) against a fresh tools run | **byte-IDENTICAL** engines and launchers (`yquake2-drm` `8cb9e7f1…`, `quake2-drm` `36dc0ef5…`, `quake3e-drm` `a18989e7…`, `quake3-drm` `af7ac68e…`, `supertuxkart-drm` `a930687f…`, `stk-drm` `c9187a39…`); every control relink byte-identical to the game port's `prog/`. Not through port_manager: that rebuilds the old game ports (and STK's 12 dependencies) in the scratch prefix |
| `vkquake_drm`, `vkcube_drm`, `kmscube_drm` | **framework build** + harness | `vkquake-drm` CODE-EQUIVALENT to the tools build (paths), `vkq-drm` IDENTICAL, the generated trampoline/GL-stub lists identical; vkcube/kmscube: same symbol sets, but the tools builds used older Mesa/libdrm snapshots (`build-out-vulkan` 11 patches, kmscube 04:51 with libdrm m3p3) — no like-for-like reference |
| `wayland` | **framework build** | `libwayland-client.a` CODE-IDENTICAL, `shmsrv` IDENTICAL |
| `weston` | **framework build** (with `mesa_drm[wayland]`) | builds, links and passes all its checks; the tools `build-out` predates today's compat/shim changes (20:16), so no current reference to compare |
| `libxshmfence_phoenix`, `libepoxy` | framework build (as dependencies) + harness | CODE-IDENTICAL to `x11-drm/build-out/xshmfence-prefix` / `xorg-drm/build-out-noshim/deps-prefix` |
| `xorg_server_drm` (+ `x11demo`) | static only (`bash -n`, `validate`, `--dry build`) | the framework build stopped in its OLD-lane dependency `xorg_server` (needed for `libmd.a`): its kdrive `Xphoenix` link fails in a from-scratch buildroot — the first real build of `xorg_server_drm` is still to do |

Sync check: `scripts/check-gpu-lane-ports-sync.sh <branch tree>` → 160 files in 45 mappings identical.

**Not converted:** the host tests (`*/hosttest`), the tools' variant/debug switches (`--relink`,
`--extra-patches`, `--variant`, `VKQDRM_EXTRA_PATCHES`), `pi/xorg-drm-m4a.sh` (a one-off cycle script), the
servers themselves (`kms`, `v3d-async` → devices, item 7) and the probes (`kmsprobe`, `exportprobe`, …).
Pending in `tools/gpu-lane/sdl2-drm/patches-vkquake`: 0008 (raster warp) and a CPU-lightmap default — copy
them into `vkquake_drm/patches/` when promoted (the sync check shows the drift; noted in the recipe).
Overlap with the Wayland half (not merged now): its `wayland_phoenix` vs this branch's `wayland`, and its
gtk3's own libepoxy vs `libepoxy`. Known debt carried into the ports, all documented in the recipes: `xorg_server_drm` depends on the
old-lane `xorg_server` port for `libmd.a` (SHA1); `yquake2_drm` / `quake3_drm` / `supertuxkart_drm` relink the
old ports' objects (item 4: fold the substitution into those ports' `p_build` when the old lane goes);
`vkquake_drm` reads the SPIR-V from the `vkquake` port's `glue/`.

**Adopting a port in an image** (after the branch is merged): list it in the project `ports.yaml` with
USE `rootfs`, e.g.

```yaml
  - name: yquake2_drm
    use: [rootfs]
```

**Merge:** phoenix-rtos-ports `feat/new-lane-graphics-ports` into `master` (new directories only — a
fast-forward-able branch off `35abace`), before `feat/new-lane-wayland-ports`: of the desktop half only
`labwc_desktop` depends on this branch (`libdrm_phoenix`, `mesa_drm[wayland]`). Overlap to resolve at that
merge: its `wayland_phoenix` covers what this branch's `wayland` port builds (libwayland + wlphx-compat), and
its gtk3 builds its own libepoxy next to this branch's `libepoxy` port — one of each should remain. Then run
`scripts/check-gpu-lane-ports-sync.sh` on the merged tree.

### Ports (Wayland desktop) — the Wayland-desktop half stored in phoenix-rtos-ports [built: dbus, wayland_phoenix]

Owner request 2026-09-27 ("make sure that all the ports (the recent ones) are correctly stored in
phoenix-rtos-ports"), second half: the Wayland desktop of the new lane — D-Bus, the Wayland base, GTK 3,
XFCE, labwc — as framework recipes on phoenix-rtos-ports branch **`feat/new-lane-wayland-ports`** (pushed
to `publish`, **not merged**; the graphics half is §4.1's `feat/new-lane-graphics-ports`). **Opt-in**: no
project `ports.yaml` names them and no existing port directory is touched (`git diff master..` of the
branch: 156 files **added** in five new directories, 0 modified or deleted), so neither `--with-ports` nor
any other default build changes. Each recipe is a transcription of its tools script at its **committed**
state: the same pinned tarballs + sha256, the same patch sets, meson/configure options, compiler flags,
private views of the ports prefix, hand links and verification gates; every compat/shim source, launcher
and config is a vendored copy under the port's `files/` (ours: BSD-3-Clause).

| Package(s) | Port | From `tools/gpu-lane/` | Licence (SPDX, as in the recipe) | Notes |
|---|---|---|---|---|
| D-Bus 1.16.2 (dbus-daemon, libdbus-1, dbus-send/-monitor/-run-session/-uuidgen) | `dbus` 1.16.2 | `dbus` | AFL-2.1 OR GPL-2.0-or-later | patch 0001, `session-phoenix{,-external}.conf`, `dbus-m7f.sh`; runtime pair of `xfce_wayland` (xfconfd by bus activation), not a build dependency |
| libwayland 1.24.0, wayland-protocols 1.45, libxkbcommon 1.7.0, wlphx-compat, libudev/libinput/libevdev shims, `<linux/input.h>` over FreeBSD's evdev codes, baked evdev/pc105/us keymap | `wayland_phoenix` 1.24.0 | `weston-drm` (non-Weston half) + `mesa-drm/compat/include`, `xorg-drm/src/phxhid_evdev_map.h` | MIT AND BSD-3-Clause AND BSD-2-Clause | also installs the glue sources + the M6 `wayland`/`seatd` patch sets (`share/wayland-phoenix/`) for `labwc_desktop`; the keymap is a committed file (no host xkeyboard-config) |
| GTK 3.24.52 (Wayland only), GLib 2.88.3 + GIO, pcre2 10.47, fribidi 1.0.16, atk 2.38.0, gdk-pixbuf 2.42.12, harfbuzz 14.4.0 (meson), pango 1.54.0, cairo 1.18.4, gtk-layer-shell 0.10.1, libepoxy 1.5.10, gtk3-hello (+ gtk3-demo, gtk3-widget-factory) | `gtk3_wayland` 3.24.52 | `gtk3-wayland` (`--usr`) + `xorg-drm/patches/libepoxy`, `xorg-drm/compat/include` | LGPL-2.1-or-later AND LGPL-3.0-or-later AND (LGPL-2.1-only OR MPL-1.1) AND BSD-3-Clause WITH PCRE2-exception AND MIT AND Apache-2.0 AND BSD-3-Clause | always `/usr`-configured (DESTDIR install); libepoxy built in-port against vendored Khronos EGL/KHR headers (no Mesa dependency: GTK draws with cairo/wl_shm); `conflicts="glib2"` (a second GLib) |
| XFCE 4.20: libxfce4util 4.20.1, xfconf 4.20.0, libxfce4ui 4.20.2, garcon 4.20.0, exo 4.20.0, libxfce4windowing 4.20.7, Thunar 4.20.10, xfce4-panel 4.20.8, xfdesktop 4.20.2, xfce4-settings 4.20.5, xfce4-appfinder 4.20.0, adwaita-icon-theme 3.38.0 (PNG), shared-mime-info 2.4 | `xfce_wayland` 4.20 | `xfce-wayland` | GPL-2.0-or-later AND LGPL-2.0-or-later AND LGPL-2.1-or-later AND (LGPL-3.0-only OR CC-BY-SA-3.0) AND BSD-3-Clause | patches thunar 0001–0002, xfce4-panel 0001, xfconf 0001, xfdesktop 0001; the GTK stack enters as a symlink-tree snapshot of `gtk3_wayland` (the tools script copies 1.6 GB); msgfmt stand-in, `pngify-icon-theme.py` |
| labwc 0.20.2, wlroots 0.20.2, foot 1.28.0, fuzzel 1.15.0, swaybg 1.2.2, tinywl + libwayland 1.24.0, wayland-protocols 1.49, libxkbcommon 1.13.2, pixman 0.46.4, libdisplay-info 0.2.0, seatd 0.9.1, libxml2 2.15.4, fribidi 1.0.16, pango 1.44.7, tllist 1.1.0, fcft 3.3.3 | `labwc_desktop` 0.20.2 | `labwc-drm` | GPL-2.0-only AND MIT AND LGPL-2.1-or-later AND BSD-3-Clause AND CC0-1.0 | depends on `mesa_drm[wayland]` (`wayland/prefix`, `wayland/link-gles.txt`) and `libdrm_phoenix` of §4.1; patches foot, fribidi, fuzzel, labwc 0001–0002, pango 0001–0003, swaybg, wayland-protocols, wlroots 0001–0004; the wallpaper is generated (`make-wallpaper.py`, CC0); committed keymap; `conflicts="gtk3_wayland"` (it links the ports GLib 2.56) |

**Conventions** — as §4.1: a private prefix (`conflicts=`; for gtk3/xfce/labwc a real one, the other
GLib), no framework CFLAGS (the tools scripts' exact flag set, `-pthread`-dropping compiler wrappers,
private dependency views), USE **`rootfs`**: everything is built into the port's own prefix, including a
`stage/` tree (+ `stage.MANIFEST`) that mirrors the target rootfs with the M7 staging's **new names only**
(`/bin/thunar-wl`, `/bin/gdbus-wl`, `/etc/xdg/labwc-xfce`, …); only `use: [rootfs]` copies it into
`_fs/<target>/root`. Two additions: each recipe's work tree mirrors the tools script's `<out>` directory
(`out/src/<pkg>`, `out/<pkg>-build`), so meson's relative source paths — the `__FILE__` strings in the
binaries — are the same; and every extracted tree is its own git repository before `git apply`/`git am`
(inside the buildroot's repository git would silently skip every path). Extra tarballs are looked up in
the port directory (gitignored), then `${PHOENIX_DISTFILES:-~/.phoenix-distfiles}/newlane/`, then fetched,
and always sha256-checked. `scripts/check-wayland-ports-sync.sh [<ports dir>]` compares every vendored
copy with its tools source (42 mappings; on the branch: identical except the three uncommitted in-flight
edits below).

**Verification.**

| Port | How | Result |
|---|---|---|
| `dbus` | `scripts/build-port.sh` in a scratch buildroot (`scripts/make-scratch-buildroot.sh`; `RPI4B_BUILDROOT=<scratch> RPI4B_PORTS_DIR=<worktree>`), dependency closure (`xorg_libs`, `zlib`, `xorg_fonts`) rebuilt from scratch there; tools `build.sh --out <tmp>` against the same sysroot | the 5 stripped programs **byte-identical** (`dbus-daemon` `0abfed003a78214d`, …); same `config.h` answers and gate |
| `wayland_phoenix` | the same scratch build; archives compared member by member after `strip --strip-debug` with the tools-way compile of the same sources | identical, except `xcursor.c.o` and xkbcommon's `context.c.o`, which bake the build-host prefix path (icon/include search path) — as every tools out dir does. (weston-drm's `build-out-g6`, the snapshot gtk3's tools build used, predates today's compat/shim sources) |
| `gtk3_wayland`, `xfce_wayland`, `labwc_desktop` | `bash -n`; shellcheck 0.11 (clean); `port_manager.py validate` (81 ports); `--dry build` resolution (`labwc_desktop` together with a snapshot of §4.1's `libdrm_phoenix`, `mesa_drm`, `wayland` definitions: resolves, `mesa_drm +wayland`); `p_prepare` of each in a throwaway directory | every tarball sha256 and every patch applies (commit counts = patch counts). **Not built through the framework**: the build host's disk (<2 GB free, gtk3's tools build-out alone is 4.9 GB) — the first real build is the check to run when there is space |

**Overlap with §4.1 (dedup after both branches are merged).** `wayland_phoenix`'s libwayland 1.24 +
wayland-protocols 1.45 + wlphx-compat half is §4.1's `wayland` port (same tarballs, same M6 patch, same
compat sources and flags); its xkbcommon 1.7 + input shims + keymap half is inside §4.1's `weston`. The
Wayland-desktop half does **not** depend on either, nor on §4.1's `libepoxy` (which needs `mesa_drm`): the
branch resolves on its own. After the merge: keep one Wayland base — e.g. `wayland` + a small
`wayland_input` (xkbcommon 1.7, shims, keymap) that `weston` and `gtk3_wayland` share — retarget
`gtk3_wayland`/`labwc_desktop`, drop `wayland_phoenix`; and decide whether GTK should link §4.1's
`libepoxy` (costs a Mesa build) or keep its Mesa-free copy.

**Not converted (yet):** the host tests (`*/hosttest`), the tools switches (`--relink`, `--until`, dbus
`--host`), and the **uncommitted in-flight work** in the tools directories at conversion time (the ports
carry the last committed state): gtk3 `patches/glib/0003-gapplication-…`, xfce `patches/xfdesktop/0002-…`,
the XFCE demo session (`build.sh`'s `stage-demo`, `pi/xfce-session`, `pi/xfce-demo-loginctl`,
`conf/xfce-demo`, `conf/labwc-xfce-demo`, the dither wallpaper); `tools/gpu-lane/atril-wayland` (a later
pass); `/etc/machine-id` (m7f staged one by hand; an image should generate it). `labwc_desktop` stages the
current `pi/labwc-desktop.sh` once, as `/bin/labwc-desktop.sh` (m7a's older copy and m7c's
`/bin/labwc-desktop-m7c.sh` name are not reproduced).

**Adopting in an image** (after the merge): list the top-level ports with USE `rootfs` — the dependencies
come along:

```yaml
  - name: labwc_desktop
    use: [rootfs]
  - name: xfce_wayland
    use: [rootfs]
  - name: dbus
    use: [rootfs]
```

**Merge:** phoenix-rtos-ports `feat/new-lane-wayland-ports` (4 commits on `35abace`, five new directories
`dbus wayland_phoenix gtk3_wayland xfce_wayland labwc_desktop`) and §4.1's `feat/new-lane-graphics-ports`
into `master`, in either order — the directory sets are disjoint. `labwc_desktop` resolves only with both
merged. Then `scripts/check-wayland-ports-sync.sh` and `scripts/check-gpu-lane-ports-sync.sh` on the merged
tree.

## 5. The migration gate

**The showcase gate, re-run on the new lane with the same drive commands, timings and HDMI checks**, on
an image where nothing of the old lane runs. Mechanics: a copy of `scripts/run-showcase-gate.sh` (never
edit the original while a gate might run) — `run-showcase-gate-drm.sh` — whose per-app entry is a *list*
of psh commands: the two servers first, then the app (psh has no `&`; the servers self-detach). Same
`wait_secs=220`, `inter_cmd_secs=8`, `max_cmd_secs=300`, `--ready-line 'V3DA srv detached|KMS srv
detached'` for the server commands (psh-interact applies the ready line to every command; the games never
print it, so they run to `max_cmd_secs` — expected), `uart-summary.sh` fault grading, `frames` from the
`flipstat … (total N)` lines, and the automatic #67 torch ROI check for vkq. Once the servers start at
boot (§4 item 7), the prelude disappears and the app list is the old one with renamed commands:

| key | old command | new-lane command | HDMI check (the same as today) |
|---|---|---|---|
| `x` | `startx_gpu action` | `/bin/bash /bin/startx-drm action` (§6.6; until the servers start at boot: `/bin/bash /bin/startx-drm --servers action`) | Window Maker + GL window animating + both xterms + xbill + xclock, the xlaunch layout |
| `qspasm` | `quakespasm` | `/usr/bin/quakespasm-drm` (flipstat relink done, §6.4) | the attract demo renders (lit, textured, HUD) |
| `q3` | `/usr/bin/quake3 +map q3dm1` | `/usr/bin/quake3-drm +map q3dm1` | q3dm1 lit, textured (lightmaps), not the main menu |
| `q2` | `/usr/bin/quake2` | `/usr/bin/quake2-drm` | demo1 in full textured 3D |
| `vkq` | `vkquake` | `/bin/vkq-drm` | start map; `check-torch-rois.py --label <label>-vkq` PASS (≥ 2 frames, both archway torches lit, viewpoint MAE < 8) |
| `stk` | `stk --track=hacienda --numkarts=4 --profile-laps=2` | `/bin/stk-drm --track=hacienda --numkarts=4 --profile-laps=2` | lit hacienda race, 4 karts, HUD |

**Pass:** 6/6 apps with `prompt=yes`, `faults=0`, `frames > 0`, the HDMI check by eye (the script's own
warning: mechanical only), torches PRESENT — on the post-migration image, with `grep -a -c` of the
old-lane strings (`v3d-winsys:`, `/dev/fb0` in GPU apps, `phxgl`, `V3DV_PHOENIX`) = 0 over the rootfs. The
fps of each app is recorded against the last old-lane gate (it is not a pass criterion, but a regression
beyond the vsync quantisation — 60/n on the new lane — is a finding to explain before deleting).

## 6. Pre-registered Pi cycles — the three new clones (+ `mig-qs`, + `mig-all`, + `mig-x`)

Common to all four: netboot image as the stk-drm cycle (core_freq=500, build ≥ 11); **single-owner rule**
— no old-lane GPU app, X or `rpi4-v3d` in the same boot; `rpi4-v3d-async-m3p2` and `rpi4-kms-gate` are
already staged from earlier cycles, as is the game data (`/usr/share/quake2/baseq2`, `/usr/share/quake3/
demoq3` with pak1 + q3key, `/usr/share/quake/id1`). ⚠ Wall clock: netboot 60–150 s + two server windows +
300 s of game ≈ 8–9 min — close to the 600 s Bash cap: run each detached (`setsid`, as the STK queues) or
from the coordinator's queue; a cycle the harness kills is void. One cycle at a time (single UART).

**Build state.** A coordinator `rebuild-rpi4b-fast.sh --scope core --with-ports --with-showcase` ran
while these clones were first built (libphoenix.a changed mid-session). All three were **re-built after it
finished (11:54)**, against libphoenix `e69b216a…` and the ports' fresh objects; both control relinks are
again byte-identical. The outputs listed in §2 are those. After any later core/ports rebuild, re-run the
three scripts (≈ 4 min together) before staging.

*§6.1–§6.4 are the first cycles as pre-registered, with the **stock** SDL swap order and the upstream
vkQuake defaults; their "28–33 fps" rows assumed one flip in flight costs a whole frame, which
[frame-pacing.md](frame-pacing.md) refuted. The adopted defaults are checked by `mig-all` (§6.5).*

**Stage (coordinator)** (`sudo install -m 755 <source> <path>`, then `cmp`; `<export>` = the live fsid=0
export, `awk '!/^#/ && /fsid=0/{print $1; exit}' /etc/exports`), sources under
`tools/gpu-lane/sdl2-drm/build-out/`. sha256 (first 16) = the **adoption rebuild** (2026-09-27, after build
17; SDL with `patches/0009`, vkQuake with 0006/0007); a staged file with any other sha is stale:

| Source | sha256 | Export path |
|---|---|---|
| `quake2-drm/yquake2-drm.stripped` | `b39f49cf6e2c4427` | `<export>/usr/bin/yquake2-drm` |
| `quake2-drm/quake2-drm` | `b08ee6a4c1088fb3` (unchanged) | `<export>/usr/bin/quake2-drm` |
| `quake3-drm/quake3e-drm.stripped` | `5fab2b12058d84f1` | `<export>/usr/bin/quake3e-drm` |
| `quake3-drm/quake3-drm` | `0f1045c2b200159f` (unchanged) | `<export>/usr/bin/quake3-drm` |
| `vkquake-drm/vkquake-drm.stripped` | `22755bb450b09e0f` | `<export>/usr/bin/vkquake-drm` |
| `vkquake-drm/vkq-drm` | `e49a7444fc782d0f` | `<export>/bin/vkq-drm` |
| `quakespasm-drm.stripped` (§6.4) | `ca2d740b82e9be1e` | `<export>/usr/bin/quakespasm-drm` |
| `stk-drm/supertuxkart-drm.stripped` | `71ac4f58a678dc20` | `<export>/usr/bin/supertuxkart-drm` |
| `stk-drm/stk-drm` | `ea3a5667004c793b` | `<export>/bin/stk-drm` |

Unstripped ELFs for addr2line (host only): `quakespasm-drm` `8f0c99658bb0e28f`, `quake2-drm/yquake2-drm`
`b0804f9b754cc0b6`, `quake3-drm/quake3e-drm` `b9f41df24240deef`, `stk-drm/supertuxkart-drm`
`ebf60a87830a60e2`, `vkquake-drm/vkquake-drm` `c656f27c61e0230d`. The stripped copies carry no symbols,
so the pacing proof is run on these: `tools/gpu-lane/sdl2-drm/gamedrm/check-swap-order.sh <unstripped ELF>`
must print `submit-first` (the builds run it and fail otherwise).

Check afterwards: `grep -a -c '<app>: new GPU lane'` = 1 on each staged engine, 0 on the shipped
`yquake2`/`quake3e`/`vkquake`; for quakespasm-drm also `grep -a -c 'quakespasm-drm flipstat'` = 1 on the
staged copy (0 on the earlier one — that is how a stale staging shows). Keep the unstripped ELFs on the
host for `addr2line`.

**Grade** (all three; ~1.3 % UART line corruption — re-read, don't count; EL0 dumps print twice):
`./scripts/uart-summary.sh <label>`, `./scripts/flipstat-summary.sh --seq <label>`, and
`grep -a -E '^(<app>|KMS |V3DA |DEBUG|ERROR|WARN|libdrm-phoenix|MESA|phxvk)' <log>`. HDMI: only snapshots
after the `<app>: new GPU lane` banner (dense ticks from it). Fault → `aarch64-phoenix-addr2line -f -e
<unstripped ELF> <pc>` first.

### 6.1 `mig-q2`

**Question:** does the unmodified yQuake2 engine (the shipped objects, byte-identical control) render
demo1 through SDL KMSDRM + Mesa GBM/EGL/GLES3 + libdrm-phoenix, and at what fps against the old lane
(38.86 fps in the gate header's run)?

```
./scripts/test-cycle-psh-interact.sh --label mig-q2 --wait-secs 220 --inter-cmd-secs 8 --idle-secs 60 \
    --max-cmd-secs 300 --ready-line 'V3DA srv detached|KMS srv detached' --ready-extra-secs 20 \
    --hdmi-dense-on 'quake2-drm: new GPU lane' -- \
    "/bin/rpi4-v3d-async-m3p2 -r 1 -m serial -i" \
    "/bin/rpi4-kms-gate -G" \
    "/usr/bin/quake2-drm"
```

| Line / observation | Predicted | If instead… |
|---|---|---|
| `V3DA srv detached`, `KMS srv detached` | once each | a server missing: staging/boot — stop |
| `ram-stage: exec /usr/bin/yquake2-drm`, then `quake2-drm: new GPU lane -- … (GLES) …` | yes | no banner: the shipped engine was exec'd (`cmp` the staging) |
| KMSDRM `DEBUG:` init (device `/dev/dri/card0`, 1 connector/encoder/CRTC), `Added packfile /tmp/quake2/baseq2/pak0.pak` | yes | `Could not initialize SDL` / no EGL config for ES 3: read the SDL `ERROR:` line; `GetPCXPalette`: data not staged |
| GL info: `OpenGL ES 3.x Mesa 26.2.0`, renderer `V3D 4.2` | yes | `kms_swrast` / llvmpipe: kmsro did not pair the render node |
| `SDL audio initialized` (C5) | yes, 0 `write STALLED` | a hang before the first swap after `SDL_OpenAudio`: C5 on the new SDL audio driver — re-run once; if repeated, `export SDL_AUDIODRIVER=dummy` A/B |
| `quake2-drm: first swap … window 1920x1080 drawable 1920x1080` | one line | a smaller window: the custom mode did not take — picture in a corner |
| `quake2-drm flipstat … fps (total N)` | **28–33 fps** over the demo windows (vsync-bound: one flip in flight + ~26 ms render ⇒ 60/2, as quakespasm-drm's 29–31) | < 25: present-path cost (`swapstat swap_us_avg` ≫ 5 ms) or a gated-flip miss (`KMS srv flipstat vbl2`); > 36: not vsync-paced — check for tearing |
| `V3DA srv qstat` | `err=0 wedges=0 rej=0` | any: FAIL |
| HDMI | demo1 in textured 3D, full screen, upright, no console bleed | black with flipstat advancing: frames land elsewhere; mirror/Y-flip: an orientation difference on this lane (finding) |
| faults | 0 kernel, 0 EL0 | addr2line first |

### 6.2 `mig-q3`

**Question:** does quake3e (shipped objects) run q3dm1 through SDL KMSDRM + Mesa desktop GL, with its
QVM JIT, and at what fps?

```
./scripts/test-cycle-psh-interact.sh --label mig-q3 --wait-secs 220 --inter-cmd-secs 8 --idle-secs 60 \
    --max-cmd-secs 300 --ready-line 'V3DA srv detached|KMS srv detached' --ready-extra-secs 20 \
    --hdmi-dense-on 'quake3-drm: new GPU lane' -- \
    "/bin/rpi4-v3d-async-m3p2 -r 1 -m serial -i" \
    "/bin/rpi4-kms-gate -G" \
    "/usr/bin/quake3-drm +map q3dm1"
```

| Line / observation | Predicted | If instead… |
|---|---|---|
| banner `quake3-drm: new GPU lane -- … (desktop GL) …` after `ram-stage: exec /usr/bin/quake3e-drm` | yes | shipped engine exec'd |
| `GL_RENDERER: V3D 4.2…`, `GL_VERSION: … Mesa 26.2.0` (compat profile) | yes | no desktop GL context: EGL `eglBindAPI(EGL_OPENGL_API)` refused — compare with quakespasm-drm's log |
| QVM: `VM_Compile` of cgame/ui/qagame succeeds (the JIT `mmap` RWX now passes through `__wrap_mmap`) | no `VM_Compile` failure, no fallback to the interpreter | interpreter fallback = the RWX mapping was refused on this path (look at `__wrap_mmap`'s pass-through); an EL0 fault in JIT code: addr2line will not help — note `pc` inside the anonymous mapping |
| `quake3-drm first swap … 1920x1080` then `flipstat` windows | **a 60/n band** (render-bound; same GPU server as the old lane) — record against the latest old-lane q3 gate log | 0 frames after the menu: `+map` ignored (q3 honours it; check the `+map q3dm1` echo) |
| q3dm7-type BIN/RENDER wedge (old-lane intermittent) | not expected on q3dm1; any `V3DA srv … wedge` | a wedge on the async server is a new-lane finding: log + stop |
| HDMI | q3dm1 lit + textured (merged lightmap atlas), HUD | black lightmaps: the UIF_XOR sampled-RT defect is in the old fork only (`external/mesa 4363822955b`) — upstream Mesa's own tiling choice applies here; a finding, not a regression of the fix |
| faults | 0 | addr2line |

### 6.3 `mig-vkq`

**Question:** does upstream vkQuake — SDL's stock KMSDRM Vulkan path, `VK_KHR_display`, Mesa v3dv behind
phxvk, no fb0 shim — load the start map, render it lit with the wall torches, and at what fps?

```
./scripts/test-cycle-psh-interact.sh --label mig-vkq --wait-secs 220 --inter-cmd-secs 8 --idle-secs 60 \
    --max-cmd-secs 300 --ready-line 'V3DA srv detached|KMS srv detached' --ready-extra-secs 20 \
    --hdmi-dense-on 'vkquake-drm: new GPU lane' -- \
    "/bin/rpi4-v3d-async-m3p2 -r 1 -m serial -i" \
    "/bin/rpi4-kms-gate -G" \
    "/bin/vkq-drm"
./scripts/check-torch-rois.py --label mig-vkq
```

| Line / observation | Predicted | If instead… |
|---|---|---|
| `vkq-drm: exec /usr/bin/vkquake-drm …`, `vkquake-drm: new GPU lane …` | yes | — |
| `vkquake-drm: SDL asked for libvulkan.so.1 -> the linked-in v3dv ICD (phxvk)`, `phxvk: new GPU lane …`, `phxvk: ICD interface version 7` | once each | `SDL_Vulkan_LoadLibrary` error: the loadso wrap not reached |
| any `vkquake-drm: GL path not linked in this binary, called: <fn>` | **none** | SDL entered its GBM/EGL branch: the window was not created `SDL_WINDOW_VULKAN` (read the fn name) |
| any `vkquake-drm: vk trampoline could not resolve <name>` | none | a direct call before `vkCreateInstance`, or a 1.1+ command the instance's API version does not enable |
| vkQuake device/driver lines (`Vulkan device: V3D 4.2…`), swapchain creation | yes, FIFO (the display WSI offers FIFO only) | `Vulkan couldn't find an appropriate plane` / `couldn't find a predefined mode`: the 1920×1080 window did not match a display mode (the `-width/-height/-fullscreen` args lost?) |
| **two card0 clients**: SDL keeps its KMSDRM fd (its `DropMaster` succeeds as a no-op) and v3dv's WSI opens card0 again | both served (rpi4-kms: 16 clients); SDL's stays idle | `KMS srv` rejects / `-EBADF` on one of them: first time with two card0 clients on hardware — a finding |
| shader modules > 4 KiB (md5.vert 5228 B) now go through Mesa's default allocator (plain `malloc`), not the port's `PL_VkHostAllocator` | no fault | a fault in `vkCreateShaderModule` = the old-lane allocator defect is real on this libphoenix: addr2line, then re-add patch hunk |
| `r_gpulightmapupdate 1` compute (`SUBMIT_CSD`) on `rpi4-v3d-async` | lightmaps lit | dark world with 3D geometry: CSD path on the async server (first Vulkan compute there) |
| `V3D_SUBMIT_CPU failed` / `VK_ERROR_DEVICE_LOST` | **never** (patch 0005 removes vkQuake's only queries) | another CPU-job path (indirect dispatch, query copy) reached G5 — record the call; the fix is G5, not another engine patch |
| `vkquake-drm flipstat … fps (total N)` | **30–60 fps** (FIFO, vsync-quantised; the port measured ~33 ms/frame render at 1080p ⇒ ~30, its on-screen 73 fps was unsynced) | < 25: present/fence path (`presentstat present_us_avg`, `KMS srv flipstat`) |
| torch ROI check | **PASS** (≥ 2 frames at the spawn viewpoint, both archway ROIs lit) | "0 at-viewpoint frames": `+map start` not reached (patch 0001's `cmdline` fix, or the demo loop took over); torches dark at the viewpoint: #67 on this lane (a real finding — the fb0 alpha cause cannot apply to an XRGB8888 plane) |
| an early `Unable to create directory <path>: …` Sys_Error | not expected | upstream `sys_sdl_unix.c` `Sys_mkdir` is fatal on anything but `EEXIST`; the port's glue made it non-fatal for the NFS root on purpose. Not a stack fault: a sixth patch (non-fatal `Sys_mkdir`) is the fix |
| faults | 0 | addr2line the unstripped `vkquake-drm` |

**Not exercised by `mig-vkq`: the exit path.** The game runs to `--max-cmd-secs` and is never quit.
Upstream `Sys_Quit` → `Host_Shutdown` → `Host_WriteConfiguration` writes `vkQuake.cfg` into the basedir
over NFS, which the port's glue skipped deliberately (the NFS large-write hang it cites); an exit cycle
(`+quit` after a timed demo, or a keyboard `quit`) is a separate check before the gate.

### 6.4 `mig-qs`

**Question:** does quakespasm-drm, relinked with the shared `gamedrm` hooks (no other change: same SDL
`libSDL2.a` `4abf34e0…`, same Mesa-GL build, same libdrm-phoenix m3p3 snapshot, same link line plus
`--wrap=SDL_GL_SwapWindow`), print the `flipstat … (total N)` lines the gate's `frames` column reads, on the
gate's own command (the attract demo, no `+timedemo`), and at the fps the poll-wake cycle measured?

**Build:** `tools/gpu-lane/sdl2-drm/build.sh --skip-mesa --skip-sdl` (≈ 1 min: engine TUs + hooks + link +
checks). `--skip-sdl` (new) reuses `build-out/sdl-prefix` as is — a plain run would recompile SDL against
the reinstalled sysroot headers and move `libSDL2.a`, which stk-drm/quake2-drm/quake3-drm/vkquake-drm also
link. Output 2026-09-27: `quakespasm-drm.stripped` 17 938 096 B **`fa40faae5acd1359…`**, unstripped
`quakespasm-drm` `e633595577d140bd…` (quakespasm embeds `__DATE__`/`__TIME__`, so every build has a new
sha). `nm -u` 0, no PT_INTERP, `__wrap_SDL_GL_SwapWindow` present, `GL_EndRendering → __wrap_SDL_GL_SwapWindow`
(a tail-call `b`), the wrapper the only caller of the real `SDL_GL_SwapWindow`, old-lane strings 0; libSDL2.a,
stk-drm, quake2-drm, quake3-drm and vkquake-drm outputs sha256-identical before and after.

```
./scripts/test-cycle-psh-interact.sh --label mig-qs --wait-secs 220 --inter-cmd-secs 8 --idle-secs 60 \
    --max-cmd-secs 300 --ready-line 'V3DA srv detached|KMS srv detached' --ready-extra-secs 20 \
    --hdmi-dense-on 'quakespasm-drm: new GPU lane' -- \
    "/bin/rpi4-v3d-async-m3p2 -r 1 -m serial -i" \
    "/bin/rpi4-kms-gate -G" \
    "/usr/bin/quakespasm-drm"
```

(m3p4's command with the gate's app line and §6's server pair; no `frames .* seconds .* fps` ready-line —
that line exists only under `+timedemo`, so the game runs to `--max-cmd-secs`, as in the gate.)

| Line / observation | Predicted | If instead… |
|---|---|---|
| `quakespasm-drm: new GPU lane -- … (desktop GL) …` then `quakespasm: main() entered` | once each (the hooks' banner is byte-identical to the old `qsdrm_banner.c` one) | two banners: a stale object with both constructors; none: shipped binary staged (`cmp`) |
| KMSDRM `DEBUG:` init lines, `GL_RENDERER` V3D 4.2, `GL_VERSION` Mesa 26.2.0 | as in m3p4 | — (nothing else changed in the link) |
| `quakespasm-drm: first swap … window 1920x1080 drawable 1920x1080 swap_interval … flipstat on` | one line | no line but the game renders: the wrap did not take — `objdump` the staged binary |
| `quakespasm-drm flipstat … fps (total N)` every 5 s, `total` rising | **28–33 fps** over the demo windows (poll-wake Finding 2: one flip in flight + ~19.5 ms GPU/frame ⇒ 30–35; timedemo measured 30.9) | < 25: the counter's own cost is ~2 `clock_gettime` per frame, so look at `swapstat swap_us_avg` and `KMS srv flipstat` first; ≥ 36: not vsync-paced |
| `./scripts/flipstat-summary.sh --seq mig-qs`; the gate's `frames` (`run-showcase-gate.sh:262`, last `(total N)` of any `flipstat` line) | both parse the lines (their patterns ignore the prefix) | `KMS srv flipstat` lines carry no `(total N)`, so they cannot be mistaken for the game's |
| `V3DA srv qstat` | `err=0 wedges=0 rej=0` | any: FAIL |
| HDMI | attract demo lit + textured, HUD | as m3p4 |
| faults | 0 | addr2line the unstripped `build-out/quakespasm-drm` |

**What the three cycles decide:** each PASS retires one old-lane game from the migration list (§3); a
FAIL is fixed in the clone's build or the stack before the gate (§5) is attempted. The gate itself runs
only after all six apps have a PASSing single cycle and the servers start at boot.

### 6.5 `mig-all` — the adopted defaults, one cycle per game

**Question:** with the adopted defaults (SDL `patches/0009` in every SDL clone; vkQuake 0006 + 0007) and the
default binaries of the §6 staging table, does each game still render as in its first cycle, at the fps the
pace/perf A/B measured? This is the last per-game check before the §5 gate; it does not replace it.

**Preconditions:** the staging table's shas `cmp`-verified on the export (a stale copy is the likeliest
false result: `pace-*`/`perf-*` variants and the first-cycle binaries all sit next to these names); servers
`rpi4-v3d-async-m3p2` + `rpi4-kms-gate` as before; the build-17 image or later. **`mig-all-q3` only on a
loader with the P10 kernel fix** — `grep -ac 'refused a payload in device memory' <TFTP>/loader.disk` ≥ 1
(build 17's `loader.disk` `f74bd59dd286c1c4` has it: 1 hit) — otherwise it repeats the mig-q3 fault storm and
is void. Five separate cycles (labels `mig-all-{q2,qs,q3,vkq,stk}`), one at a time, each ≈ 8–9 min: run them
detached or from the queue (§6 wall-clock note). Record `loader.disk`'s sha per cycle: the image differs from
the first cycles' (build 14–15), so first-cycle-vs-mig-all is not a one-variable A/B; pace-/perf- vs
mig-all is (same patches, only build 17 and the Mesa-GL set `278cdef4…` → `4a457a1e…` differ, §2).

```
./scripts/test-cycle-psh-interact.sh --label mig-all-q2 --wait-secs 220 --inter-cmd-secs 8 --idle-secs 60 \
    --max-cmd-secs 300 --ready-line 'V3DA srv detached|KMS srv detached' --ready-extra-secs 20 \
    --hdmi-dense-on 'quake2-drm: new GPU lane' -- \
    "/bin/rpi4-v3d-async-m3p2 -r 1 -m serial -i" \
    "/bin/rpi4-kms-gate -G" \
    "/usr/bin/quake2-drm"

./scripts/test-cycle-psh-interact.sh --label mig-all-qs --wait-secs 220 --inter-cmd-secs 8 --idle-secs 60 \
    --max-cmd-secs 300 --ready-line 'V3DA srv detached|KMS srv detached' --ready-extra-secs 20 \
    --hdmi-dense-on 'quakespasm-drm: new GPU lane' -- \
    "/bin/rpi4-v3d-async-m3p2 -r 1 -m serial -i" \
    "/bin/rpi4-kms-gate -G" \
    "/usr/bin/quakespasm-drm"

# requires a loader with 'refused a payload in device memory' (P10 kernel fix)
./scripts/test-cycle-psh-interact.sh --label mig-all-q3 --wait-secs 220 --inter-cmd-secs 8 --idle-secs 60 \
    --max-cmd-secs 300 --ready-line 'V3DA srv detached|KMS srv detached' --ready-extra-secs 20 \
    --hdmi-dense-on 'quake3-drm: new GPU lane' -- \
    "/bin/rpi4-v3d-async-m3p2 -r 1 -m serial -i" \
    "/bin/rpi4-kms-gate -G" \
    "/usr/bin/quake3-drm +map q3dm1"

./scripts/test-cycle-psh-interact.sh --label mig-all-vkq --wait-secs 220 --inter-cmd-secs 8 --idle-secs 60 \
    --max-cmd-secs 300 --ready-line 'V3DA srv detached|KMS srv detached' --ready-extra-secs 20 \
    --hdmi-dense-on 'vkquake-drm: new GPU lane' -- \
    "/bin/rpi4-v3d-async-m3p2 -r 1 -m serial -i" \
    "/bin/rpi4-kms-gate -G" \
    "/bin/vkq-drm"
./scripts/check-torch-rois.py --label mig-all-vkq

./scripts/test-cycle-psh-interact.sh --label mig-all-stk --wait-secs 220 --inter-cmd-secs 8 --idle-secs 60 \
    --max-cmd-secs 440 --ready-line 'V3DA srv detached|KMS srv detached|profile: Number of frames' --ready-extra-secs 30 \
    --hdmi-dense-on 'stk-drm: new GPU lane' -- \
    "/bin/rpi4-v3d-async-m3p2 -r 1 -m serial -i" \
    "/bin/rpi4-kms-gate -G" \
    "/bin/stk-drm --track=hacienda --numkarts=4 --profile-laps=2"
```

(q2/qs/q3/vkq: the §6.1–§6.4 commands with the `mig-all-` labels; stk: the `stkdrm-1` command of
[M3](M3-libdrm-phoenix.md) with §6's `--wait-secs`. Grade as in §6; STK by the M3 rule — mean of the
per-window `stk-drm flipstat` fps over the gameplay windows, ≥ 10 windows.)

| Cycle | Line / observation | Predicted | If instead… |
|---|---|---|---|
| all | banner `<app>: new GPU lane …` once, after the launcher's `exec /usr/bin/<engine>-drm` | yes | shipped or variant engine exec'd: staging (`cmp`) |
| all GL | SDL errors `Wait for previous pageflip failed` / `Could not queue pageflip` / `eglSwapBuffers failed` / `Could not lock front buffer` | **0** | any: patch 0009's buffer accounting (EBUSY = two flips pending) — as frame-pacing §7.1 |
| `mig-all-q2` | `quake2-drm flipstat` steady windows | **60.00** (every steady window ≥ 58; pace-q2 gave 60.00 in 8/8) | exactly 30.00 with `swap_us_avg` ≈ 24 500: the staged engine is not this build (run `check-swap-order.sh` on the unstripped twin, `cmp` the staging); 38–55: GPU slower at the higher rate — read `V3DA srv qstat` render ms/job |
| `mig-all-q2` | `KMS srv flipstat` | `vbl1` ≥ 90 % of flips (pace: 2659/2692) | `vbl2` dominant: as the 30.00 row |
| `mig-all-qs` | `quakespasm-drm flipstat` | **mean ≈ 46** (40–52), windows 36–53, never > 60.1 (pace-qs 46.05, 36.8–52.4) | ≈ 30: stale staging as above |
| `mig-all-q3` | kernel: no `Data Abort (EL1)`; `msg: refused a payload in device memory (… from quake3e-drm …)` | 0 EL1 faults; the refusal line **may** appear (it is the fix acting on the uncached buffer mig-q3 sent) — record how often and what quake3e-drm then does (an `EFAULT`-type error on that write, not a crash) | an EL1 fault storm: the loader lacks the fix (check its sha/string first) |
| `mig-all-q3` | `quake3-drm flipstat` | **unknown** (first run past the P10 point): a band ≤ 60.1 over q3dm1; old-lane quake3 30+ | 0 frames after the menu: `+map` ignored; > 60.1: not vsync-paced |
| `mig-all-q3` | HDMI | q3dm1 lit + textured, HUD | as §6.2 |
| `mig-all-vkq` | `Using R8G8B8A8 color buffer format (V3D: …)`, no `A2B10G10R10` line | yes | 0007 not in the staged binary |
| `mig-all-vkq` | `vkquake-drm flipstat` | **≈ 17** (15–19; perf-vkq-b 17.06; the SDL reorder does not touch the Vulkan WSI path) | ≈ 10.4: the pre-0006 binary is staged; ≈ 15.5: 0007 missing |
| `mig-all-vkq` | torch ROI check | PASS or INCONCLUSIVE for the viewpoint reason (mig-vkq, perf-vkq-b) | torches dark at the viewpoint: finding |
| `mig-all-stk` | `stk-drm flipstat` (M3 rule) | **11.5–12.5** (stkdrm 11.89; the reorder is expected neutral to slightly positive: STK's ~10 jobs/frame are mostly submitted mid-frame by its FBO passes, frame-pacing §8) | > 13: the final present *was* on STK's critical path (a gain, record it); < 11: diff `V3DA srv qstat` against stkdrm-1 |
| `mig-all-stk` | exit | `profile: Number of frames …`, prompt back | one EL0 fault **at exit** in `fflush`/`_atexit_finalize` = the known libphoenix `fclose(stdout)` UAF (M3; branch `fix/stdstream-fclose-uaf`) unless that branch is in the image — not a pacing regression |
| all | `V3DA srv qstat` err/wedges/rej; faults | 0 / 0 / 0; 0 (stk: see exit row) | addr2line the unstripped twin (§6 table) first |
| all GL | HDMI | as the game's first cycle, no torn frames | a torn frame: frame-pacing §5's invariants violated — stop |

**Decides:** all five as predicted (q3: renders without the fault) → the new-lane game set is final for the
§5 gate. A game at its old stock-order fps → staging, not the patch (pace/perf already proved the patches).

### 6.6 `mig-x` — the X desktop (`startx-drm action`) on Xorg-drm (§3 blocker 3)

**Question:** does the old gate's X scene — Window Maker + the GL window + xbill + xclock (+ the two
xterms) — come up on the new lane (Xorg-drm-noshim, modesetting + glamor on V3D, DRI3/Present GL client,
phxhid input, `rpi4-kms -C` console handover), hold 200 s with 0 faults, and tear down cleanly?

**The launcher** `tools/gpu-lane/xorg-drm/pi/startx-drm` (bash; psh has no `&`/`;`/`|`) [built]:
`/bin/bash /bin/startx-drm [--servers] [action|wmaker]`. Starts `/bin/Xorg-drm-noshim :1 -config
/etc/X11/xorg-drm.conf -terminate -ac -nolisten tcp` (the committed `xorg-drm/conf/xorg-drm.conf`,
`cmp`-identical to the staged one: modesetting on `/dev/dri/card0`, `AccelMethod glamor`,
`DefaultDepth 24`, SW cursor, phxhid on `/dev/kbd0` + `/dev/mouse0`; the compiled-in font path is
xlaunch's `-fp` = misc,75dpi), waits for `/tmp/.X11-unix/X1` (early exit if the server dies), then
the clients of `pl_phoenix_xlaunch.c`'s `action` mode in its order, with its geometries and environment
(`HOME=/root PATH=/bin XFILESEARCHPATH XLOCALEDIR`, `DISPLAY=:1`):

| # | client | command | lands (1920×1080) |
|---|---|---|---|
| 0 | Window Maker | `/bin/wmaker` (then `WM_SETTLE`=3 s) | dock top-right, clip top-left |
| 1 | GL window "Phoenix V3D GL" | `/bin/eglx11-demo-x`, `XDEMO_GEOM=640x480+300+180 XDEMO_INTERVAL=1 XDEMO_TITLE="Phoenix V3D GL" XDEMO_EGL_DEBUG=0` | (300,180): where the old `gl-x11-window-daemon` is in every old-lane gate frame (it ignores xlaunch's `-geometry 640x480+20+30` and hints 300,180 itself — `gl_x11_window.c:272`; b18 gate frame `20260927-182222-b18-gate-x-tick.png`) |
| 2 | xterm + Life | `/bin/xterm -geometry 96x28+20+560 -e /bin/python3 /usr/share/demo/life.py --log /var/log/life.log` | row 2 left |
| 3 | xclock | `/bin/xclock -geometry 190x190+1500+30` | top right |
| 4 | xbill | `/bin/xbill -geometry 400x460+945+30` | ≈(945,0) (Window Maker auto-places it; Xt sets PPosition) |
| 5 | xterm + top | `/bin/xterm -geometry 96x24+690+560 -e /bin/top` | row 2 middle |

The `action` scene has **no xcalc** (xcalc is in xlaunch's `showcase`/`deskapps` modes); the launcher
follows the source. Then a hold (`HOLD`, default 200 s; `0` = until the WM exits, the interactive
mode) with an `XDRM hold` heartbeat every 10 s, and a teardown: SIGTERM each client in reverse order,
the WM last (`XDRM client exited name=… rc=…`), `-terminate` ends the server (else TERM after 15 s),
`XDRM server exited rc=… socket=gone`, `XDRM done rc=0 reason=hold-done`. `--servers` starts whichever
new-lane server is not running (`/dev/v3d-async`, `/dev/kms`, `shmsrv -s`), so one psh command brings
the desktop up — what the gate needs until §4 item 7. Host dry-run (stub server/clients, paths
rewritten into a temp root): full `action` start → hold → teardown order → server TERM fallback, the
`wmaker` mode's WM-exit path, and the early exit of a server that dies before its socket — all PASS.

**The GL client change** (`x11-drm/src/eglx11_demo.c`): the window now carries `WM_NORMAL_HINTS`
`USPosition|USSize|PPosition|PSize` from `-g`/`XDEMO_GEOM` — without them Window Maker auto-places it
(m4p2a honoured the position only because no WM ran) — and a title knob (`-t`/`XDEMO_TITLE`, default
`eglx11-demo`, so m4p2a's behaviour is unchanged). Built into a new dir, `x11-drm/build.sh --out
tools/gpu-lane/x11-drm/build-out-x` (same Mesa `--x11` build `7373c40f…`, libdrm m5b, current
libphoenix): `nm -u` 0 and every build.sh check as before; `XSetWMNormalHints` linked; old-lane strings 0.
Its `libxshmfence.a` differs from `build-out/`'s `4049c5b0…` only in DWARF paths (`build-out-x/src/…`):
objdump text/rodata/data of both members identical — the fence layout Xorg-drm-noshim links. `build-out/`
untouched (`eglx11-demo-stripped` still `f7a5bb38…`).

**Input, checked statically against the kdrive driver** (`ports/xorg_server/files/ddx/fbdev.c:574-913`
vs `xorg-drm/src/phxhid.c`) [read]: same devices and open flags (`/dev/kbd0` `O_RDWR|O_NONBLOCK`,
40 × 25 ms retries; `/dev/mouse0` `O_RDONLY|O_NONBLOCK`); same raw-mode request (one `0x01` byte →
usbkbd 8-byte boot reports); same report diff (modifier bits, releases before presses) and the **same
HID→evdev table** (`diff` of `hid_evdev_map.h` / `phxhid_evdev_map.h`: only the include guard);
X keycode = evdev + 8 in both (xf86 `is_down` vs kdrive `is_up` polarity both correct); mouse HID bits
L/R/M → X 1/3/2 in both; same 64-byte bounded drains on a main-thread timer (10 ms vs 16 ms);
phxhid adds the wheel (buttons 4/5), kdrive dropped byte 3. **One behavioural difference:** kdrive
freed `/dev/kbd0` itself (`FBCONSETMODE(FBCON_DISABLED)` at screen init); on the new lane
`rpi4-kms -C` sends the same ioctl to `/dev/tty0` (`kms_fw.c:101`), but only once a plane shows an fb
(`kms_main.c:522-538`). The ordering holds: modesetting's `CreateScreenResources` →
`drmmode_set_desired_modes` runs before `InitInput` (`dix/main.c:248`); pl011-tty's bridge closes kbd0
within `PL011_TTY_KBD_POLL_US` = 8 ms of `kbdReleased` (`pl011-tty.c:1152`), well inside phxhid's
1 s retry window; usbkbd resets `rawMode` on close (`usbkbd.c:553`), so the console bridge reopens a
cooked device after X exits. Without `-C` all five m4 logs show `PHXHID dev=/dev/kbd0 … open=Device or
resource busy` and `dev=/dev/mouse0 … open=ok` — expected (M4 R5). **Nothing was missing; no code change.**

**Stage** (done 2026-09-27; `EXPORT=/srv/phoenix-rpi4-nfs-gcc16`, new names only, `cmp` OK):

| Source | sha256 (first 16) | Export path |
|---|---|---|
| `tools/gpu-lane/xorg-drm/pi/startx-drm` | `32f1d951b6202a9f` | `$EXPORT/bin/startx-drm` |
| `tools/gpu-lane/x11-drm/build-out-x/eglx11-demo-stripped` | `324a2d14757404e1` | `$EXPORT/bin/eglx11-demo-x` (m4p2a's `/bin/eglx11-demo` `f7a5bb38…` stays) |
| already staged: `Xorg-drm-noshim` `fdf44b91a1bf3227` (m4n), `/etc/X11/xorg-drm.conf` (= repo conf), `shmsrv` `6a89f2610a5ad80d`, `rpi4-v3d-async-low`, `rpi4-kms-g7`, bash, wmaker/xterm/xclock/xbill/python3/top, `life.py` | — | unchanged |

addr2line (host): `tools/gpu-lane/x11-drm/build-out-x/eglx11-demo` (`ef718010197a7451`),
`tools/gpu-lane/xorg-drm/build-out-noshim/Xorg-drm`.

**Cycle** (one netboot cycle, ≈ 6–8 min: run detached or from the queue; single-owner rule — no old-lane
GPU app, X or `rpi4-v3d` in the boot):

```
./scripts/test-cycle-psh-interact.sh --label mig-x --wait-secs 220 --inter-cmd-secs 8 --idle-secs 60 \
    --max-cmd-secs 300 --ready-line 'V3DA srv detached|KMS srv detached|SHMSRV srv detached|XDRM done' \
    --ready-extra-secs 20 --hdmi-dense-on 'XDRM desktop up' -- \
    "/bin/rpi4-v3d-async-low -r 1 -m serial -i" \
    "/bin/rpi4-kms-g7 -G -p 96 -C" \
    "/bin/shmsrv" \
    "/bin/bash /bin/startx-drm action" \
    "/bin/shmsrv -s"
```

Grade: `./scripts/uart-summary.sh mig-x`; `grep -a -E '^(XDRM|XDEMO|PHXHID|KMS |V3DA |SHMSRV )|console
handover|kbd bridge|glamor X|\((EE)\)|Exception #' <log>`. HDMI: only snapshots after `XDRM desktop up`.

| Line / observation | Predicted | If instead… |
|---|---|---|
| `KMS srv ready … console_off=1 …`, the three `srv detached` lines | once each | `console_off=0`: `-C` lost (staging of `rpi4-kms-g7`) |
| `XDRM precheck v3d-async=up kms=up shm=up`, `XDRM socket=up wait_s=<1–20>` | yes | `socket=missing … server=exited`: the m4c table (M4 §10) |
| Xorg: `glamor X acceleration enabled on V3D 4.2.14.0`, `Initializing extension DRI3` / `Present`, `(**) modeset(0): Depth 24` | as m4n-noshim | — |
| `KMS srv console handover disable rc=0` after Xorg's modeset, before the `PHXHID` lines | once | no line: no plane shown (black screen too) — or `-C` absent |
| `PHXHID dev=/dev/kbd0 type=keyboard open=ok fd=… raw=1 tries=<0–40>`, `PHXHID dev=/dev/mouse0 type=mouse open=ok` | **both ok** (first `-C` run on hardware) | kbd0 `Device or resource busy`: the handover came after phxhid's 1 s retries (kms applies the first commit late) — a finding, the desktop still passes; `raw=0`: usbkbd refused the mode byte |
| `XDRM client start name=…` × 6, `XDRM desktop up mode=action clients=6 live=<4–6>` | yes | `live` < 4: read the dead client's `XDRM client exited … (earlier)` rc at teardown |
| both xterms: `xterm: fatal pty error errno=22 …` possible | **either outcome is not a lane finding**: the old lane's b18 gate hit exactly this (`rpi4b-uart-20260927-181626-b18-gate-x.log`: both xterms died, so its reference frame shows no xterm) | an xterm up with Life / top running = better than the old gate |
| `XDEMO window … title="Phoenix V3D GL" hints=USPosition\|USSize`, `XDEMO gl renderer="V3D 4.2.14.0"`, `XDEMO first_swap ok` | yes | `done rc=3 reason=no-shmsrv`: shmsrv not up |
| `XDEMO fps=` every 2 s | **≈ 60.00** (interval 1; m4p2a 60.00 with no WM; the other clients are idle 2D) | 40–55: Present copies competing with Window Maker / xbill redraws on the serial render queue — record `swap_avg_ms`; ≤ 30: finding |
| `XDRM hold … live=…` every 10 s for 200 s | yes | the heartbeat stops: the launcher (bash) wedged — check the last `XDEMO fps` line time |
| HDMI (after `desktop up`) | the b18 gate frame's scene on the new lane: Window Maker clip (top-left) + dock (top-right) + miniwindow icons, **"Phoenix V3D GL" 640×480 at ≈(300,180)** with the hexagon turning between snapshots (colours/angle differ), **xbill ≈(945,0)**, **xclock top-right (1500,30)**, the xterms at row 2 if they started, SW cursor; no console text over the desktop | GL window elsewhere: the WM hints did not take (staged the old `eglx11-demo`?); black window with fps advancing: M4 §P2.6's rows apply; console text bleeding through: `-C` handover missing |
| `XDRM teardown reason=hold-done`, `XDRM client exited name=… rc=0` (xterms/wmaker/xbill/xclock 0 or 143, `gl` 0 = `XDEMO done … stop=signal`) | yes | `gl rc=142`: a swap stuck at teardown (`alarm(5)`), note it |
| `XDRM server exited rc=0 socket=gone`, then `KMS srv console handover enable rc=0`, `pl011-tty: kbd bridge opened /dev/kbd0`, `XDRM done rc=0 reason=hold-done`, the psh prompt | yes (`-terminate` after the last client; no `still up … sending TERM`) | `still up`: a client (wmaker helper) kept a connection — the TERM path still ends it; a kernel wedge at exit: the known scheduler-printf deadlock class (memory: X desktop-exit wedge ~1 in 6, lane-independent) — record, re-run once |
| `SHMSRV stats rc=0 live=0` | fence objects released | `live>0`: a descriptor leaked |
| faults | 0 kernel, 0 EL0 (life.py/top may linger as orphans after their xterm dies — no ps/pkill; they are not faults) | addr2line `build-out-x/eglx11-demo` / `build-out-noshim/Xorg-drm` first |

**Bench-only row `mig-x-input`** (needs a person at the Pi: a USB keyboard + mouse plugged in; same
commands with `export HOLD=0` before the launcher, interactive): move the mouse → `PHXHID first mouse
event dx=… dy=…`, the SW cursor moves across the HDMI snapshots; click an xterm (focus) and type
`ls` Enter → `PHXHID first keyboard event keycode=…`, the characters appear in that xterm (evdev+8
keymap: `a` = X 38); Window Maker's root menu on a right click (button 3); wheel in xterm scrolls; exit
from Window Maker's menu → `XDRM teardown reason=wm-exited`, console text returns and psh takes USB
keyboard input again (usbkbd back in cooked mode). Predicted: all of these; a keyboard that types nothing
while `PHXHID … kbd0 … open=ok raw=1` and mouse works = focus/keymap finding, not the open path.

**Decides:** a PASS closes §3 blocker 3 (the X half of the §5 gate has a new-lane command). The gate
switch is the one line in §5's table — in `scripts/run-showcase-gate.sh` (a copy, per §5; never the
original while a gate might run): `"x:startx_gpu action"` → `"x:/bin/bash /bin/startx-drm --servers
action"` (drop `--servers` once the servers start at boot). The `x` row's grading stays as today: the
`frames` column is exempt for `x` (`XDEMO fps=` lines carry no `flipstat … (total N)`), HDMI by eye,
faults from `uart-summary.sh`; the launcher's own exit (`XDRM done rc=0`, prompt back) now lands inside
`max_cmd_secs=300` (≈ 20 s start + 200 s hold + ≤ 25 s teardown).

## 6r. Results — `mig-q2`, `mig-q3`, `mig-vkq` (queue37, 2026-09-27 12:53–13:14)

| cycle | log | result | fps (new / old lane) | notes |
|---|---|---|---|---|
| `mig-q2` | `rpi4b-uart-20260927-125308-mig-q2.log` | ✅ renders demo on HDMI (`…-130003-mig-q2-tick.png`), 0 exceptions, 0 `MESA` errors | **30.00** every window / 38.86 unsynced | `swapstat swap_us_avg≈24 500`: vsync-locked double buffering; the frame just misses one vblank, so every frame costs two. → [frame-pacing.md](frame-pacing.md) (agent) |
| `mig-q3` | `rpi4b-uart-20260927-130015-mig-q3.log` | ✗ **kernel fault storm**: `Data Abort (EL1)` in `pl011-tty`, `hal_memcpy` ← `msg_map` (`proc/msg.c:134`), alignment fault on an uncached sender page, ~4200 dumps over 300 s, right after `6 bots parsed` / an ANSI escape | — / 30+ | new register row **P10**; kernel fix + which quake3-drm buffer is uncached: agent, [misc/2026-09-27-el1-msg-map-uncached-fault.md](../misc/2026-09-27-el1-msg-map-uncached-fault.md) |
| `mig-qs` (queue40, 13:59) | `rpi4b-uart-20260927-135946-mig-qs.log` | ✅ renders, 0 exceptions; flipstat present (blocker 2 closed) | **30.3** (vsync-locked, as mig-q2) / ~40 (memory: old-lane quakespasm ~40 fps) | same two-vblank pacing → [frame-pacing.md](frame-pacing.md) |
| `mig-q3-fix` (queue46, build 17 with the P10 kernel fix) | `rpi4b-uart-20260927-163009-mig-q3-fix.log` | ✅ plays q3dm1; 0 EL1 dumps; the kernel refused 2 device-memory writes **from the render server** (`rpi4-v3d-async-m3p2`) instead of looping | **59.8** (vsync) / 30+ | not a device buffer: the server's stdout buffer (RAM) was misread as device memory by a kernel `vm_mapFlags` lookup, fixed in kernel `3da3fb38`; cycle `v3da-devlog` ([§8](../misc/2026-09-27-el1-msg-map-uncached-fault.md)) |
| `mig-vkq` | `rpi4b-uart-20260927-130722-mig-vkq.log` | ✅ renders the start map, lit, torches visible (`…-131422-mig-vkq-tick.png`); 0 exceptions; `phxvk: first present result=0` | **10.4** steady (45 windows) / 73 unsynced | ✗ 2.2× slower like-for-like (7× only against the old lane's `scr_showfps` capture) → [vkquake-perf.md](vkquake-perf.md). ROI torch check INCONCLUSIVE (no frame at the reference viewpoint, mae 13.4 > 8.0) |

**Decides:** the SDL KMSDRM route works for both GL games that got a result, and so does SDL's Vulkan/`VK_KHR_display` route. The gate
cannot pass yet: quake3-drm crashes the kernel (P10), and vkQuake is 7× slower than the old lane.

**Follow-ups, both Pi-proven and adopted (2026-09-27):** `pace-q2` / `pace-qs` (queue44) — the SDL swap
reorder takes quake2-drm 30.00 → **60.00** and quakespasm-drm 30.0 → **46.1** ([frame-pacing.md](frame-pacing.md));
`perf-vkq-a` / `-b` (queue43) — `r_oit 0` + RGBA8 take vkquake-drm 10.4 → 15.5 → **17.1** ([vkquake-perf.md](vkquake-perf.md)).
Both are now in the default builds (`sdl2-drm/patches/0009`, `patches-vkquake/0006–0007`); every SDL clone
was rebuilt (§6 staging table) and `mig-all` (§6.5) is the combined check. q3 still waits on P10's
kernel fix in the image (in build 17's loader).

**vkQuake follow-up →** [vkquake-perf.md](vkquake-perf.md): like-for-like the regression is 2.2× (old-lane `flipstat` 22.9 fps median, not the 73 of `scr_showfps`); GPU 74 ms/frame (5 full-screen render jobs from WBOIT + the UI/post-process pass, 16F RGB10A2 tiles; compute 28 ms) serialised with ~22 ms of CPU. Variants `vkquake-drm-perf-a` / `-perf` staged, cycles `perf-vkq-a` / `-b` pre-registered there.

## 6s. Result — `mig-all` (chain52, build 18, 2026-09-27 18:55–19:33): 5 of 5 games run on the new lane

Default binaries after the adoption (SDL `submit-first`, vkQuake 0006+0007), staged from `migall-frozen`; kernel
build 18 (P10 + vm_mapFlags + fork fix). Every cycle: 0 exceptions, 0 EL1 dumps, 0 refused payloads.

| cycle | fps median (steady windows) | old lane (like-for-like) | log |
|---|---|---|---|
| mig-all-q2 | **60.00** (n=57) | 38.86 | `*-mig-all-q2.log` |
| mig-all-qs | **44.45** (n=59, 22–57) | ~40 | `*-mig-all-qs.log` |
| mig-all-q3 | **59.40** (n=59) | 30+ (P10 blocked it on the new lane until build 17) | `*-mig-all-q3.log` |
| mig-all-vkq | 17.11 (n=45, 15.5–18.7) | **22.9** | `*-mig-all-vkq.log` |
| perf-vkq-f2 (0008 raster warp + CPU lightmaps; not yet promoted) | **29.70** (n=111; flipstat 30.00 = half-vblank) | **22.9** | `*-perf-vkq-f2.log`, [vkquake-perf.md](vkquake-perf.md) |
| pace-vkq-g (promoted `vkq-drm-g` + `+vid_vsync 2` = 3 images) | **44.21** (n=113, deduplicated) | **22.9** | `*-pace-vkq-g.log`, [vkquake-perf.md](vkquake-perf.md) |
| mig-all-stk | **12.43** (n=65) | 8.3 (Pi OS: 11.7) | `*-mig-all-stk.log` |

**Decides:** the migration gate's game half passes for 4 of 5 at or above the old lane. vkQuake is the one
regression left (compute path: [vkquake-perf.md](vkquake-perf.md)). Still open before retiring the old lane: the X
desktop on Xorg-drm (§3 blocker 3: an `action` launcher, input), the HDMI checks per game in the gate format,
and vkQuake. The Wayland desktop (M7) is an addition, not a gate item.

## 6t. Result — `mig-x` (chain61, build 18, 2026-09-27 21:07–21:12): ✅ the X desktop on the new lane

Log `artifacts/rpi4b-uart/rpi4b-uart-20260927-210731-mig-x.log`; HDMI `artifacts/hdmi/20260927-211335-mig-x-tick.png`: the old gate's `action` scene on Xorg-drm
(modesetting + glamor): Window Maker (dock + clip), **"Phoenix V3D GL" (eglx11-demo over DRI3/Present) at 60.00 fps
vsynced** (old lane windowed GL ≈ 14 fps), xbill, xclock. Input: `phxhid keyboard on /dev/kbd0`, `phxhid mouse on
/dev/mouse0` (the `-C` console handover works). The two xterms died with `fatal pty error errno=22`, exactly as in
the old lane's gate (a shared pty issue, not a migration regression). Teardown: `XDRM teardown reason=hold-done`,
all clients down, `server exited rc=0 socket=gone`, `XDRM done rc=0`; 0 exceptions / EL1. Blocker 3 is closed in its
automated form; the bench-only `mig-x-input` row (typing, clicking) remains.

**Migration gate status:** games 5/5 (vkQuake still slower: 17 vs 23), X desktop ✅, Wayland desktop (M7) ✅ as an
addition. Left before retiring the old lane: vkQuake, the pty errno=22 (both lanes), the bench rows, and the gate
script switch (`x:/bin/bash /bin/startx-drm --servers action`).

## 7. Deletion list — the old lane (after the gate passes)

Delete only after §5 passes on the migrated image; one sibling commit per repo, then a coordination
manifest.

| What | Where | Note |
|---|---|---|
| In-process winsys + Phoenix Mesa glue | `sources/phoenix-rtos-devices/gpu/rpi4-v3d/mesa/` (`v3d_phoenix_winsys.c`, `v3d_libdrm_shim.c`, `v3dv_libdrm_shim.c`, `vk_icd_link.c`, stubs, `build-{gl,v3d,v3dv}-phoenix.py`, shim headers) | the `v3d-winsys:` strings, flipstat, `peek_next_scanout`, the firmware-pan present |
| Old GPU daemon | `sources/phoenix-rtos-devices/gpu/rpi4-v3d/` (`rpi4-v3d.c`, `v3d_gpu.c`, `libv3d-client.*`, `/dev/v3d-srv`) | used only by `startx_gpu`'s glamor Xphoenix |
| Mesa fork + prebuilt archives | `external/mesa` phoenix branch; `tools/.gpu-libs/`; `/tmp/mesa-v3d-build` | superseded by the mesa-drm port (upstream 26.2.0 + 12 patches) |
| SDL `/dev/fb0` video backend + GL glue | `sources/phoenix-rtos-ports/sdl2` (the Phoenix video driver, `glue/sdl_phoenix_glctx.c`, `glue/sdl_phoenix_glstubs.c`) | sdl2 recipe = the sdl2-drm build |
| kdrive X | `xorg_server` port's kdrive `Xphoenix`, the fbdev DDX + glamor shim (`files/ddx/fbdev.c`), `Xphoenix-glamor-daemon`; `tools/x11-port/ddx`, `build-xfbdev.sh`, `build-xserver-core.sh`; `pl_phoenix_xlaunch`'s rpi4-v3d start-up | replaced by `xorg_server_drm` |
| Old GL-in-X client | `tools/x11-port/gl_x11_window.c`, `build-gl-x11-window.sh`, `/bin/gl-x11-window-daemon` | KNOWN-ISSUES G1 closes with it |
| vkQuake fb0 glue | `sources/phoenix-rtos-ports/vkquake/glue/pl_phoenix_*.c`, `vk_trampolines.c`, `sdl-shim/`, `vkq_phoenix_compat.h`, patch 0001 (and the proposed 0002); `tools/vkquake-port/` | keep `vkquake_shaders.c` only if not regenerated |
| Game-port GL wiring | the `libGL-phoenix`/`libv3d-phoenix`/`sdl2/glue` blocks in `ports/{quakespasm,yquake2,quake3,supertuxkart}/port.def.sh` | replaced by the new-stack link |
| `/dev/fb0` server | `sources/phoenix-rtos-devices/video/rpi4-fb` + its `user.plo.yaml` lines (nfsroot + sd) | only if §4 item 9 ports `hevc-play`; else it becomes rpi4-kms's fbdev emulation |
| plo triple-height firmware fb | plo graphmode sizing, `gpu_mem=128` rationale | reduce after measuring (fbcon only) |
| fbcon's SDL hooks | `FBCONSETMODE` users in the old SDL video backend | fbcon itself **stays** (pl011-tty + teken); rpi4-kms `-C` uses the same ioctl |
| New-lane scaffolding that the ports supersede | `tools/gpu-lane/{sdl2-drm,vulkan-drm,v3d-async/build-*-v3da.sh,…}` clone scripts, the `*-v3da` clones | after the ports build the same binaries; keep the probes (drmprobe, kmstest) |
| Docs | KNOWN-ISSUES rows that only describe the old lane (G1 windowed GL, C1 in-process-winsys items where proven lane-specific, vcmbox flicker) | move to `docs/done/` with the migration result |

**Checks after deletion:** `grep -rn` over the port recipes and `user.plo.yaml` for `gpu-libs`,
`libGL-phoenix`, `libv3d-phoenix`, `sdl_phoenix_glctx`, `rpi4-v3d\b`, `Xphoenix` = 0; the rootfs string
scan of §1 = 0 for `v3d-winsys:`, `phxgl`, `V3DV_PHOENIX`, `RPI4FB_GETMODE`; a clean Docker `--no-cache`
build (the clean-build release gate) and the §5 gate once more on that image.

## 8. Files (this pass)

| Path | What |
|---|---|
| `tools/gpu-lane/sdl2-drm/build-quake2-drm.sh`, `build-quake3-drm.sh` | the two relink builds (thin wrappers) |
| `tools/gpu-lane/sdl2-drm/gamedrm/relink-sdl-gl-game.sh` | their shared body: control relink, substitution, proofs, launcher |
| `tools/gpu-lane/sdl2-drm/gamedrm/gamedrm_hooks.c` | banner + SDL DEBUG logging + `flipstat`/`swapstat` (BSD-3) |
| `tools/gpu-lane/sdl2-drm/build-vkquake-drm.sh` | SDL-Vulkan variant + upstream vkQuake + link + proofs + launcher |
| `tools/gpu-lane/sdl2-drm/patches-sdl-vulkan/0001-cmake-phoenix-kmsdrm-vulkan.patch` | `SDL_VULKAN` on Phoenix (applied only by build-vkquake-drm.sh) |
| `tools/gpu-lane/sdl2-drm/patches-vkquake/0001–0007` | the port's four non-video engine fixes, split per file, + 0005 (no timestamp queries until G5) + 0006/0007 (`r_oit 0`, RGBA8 colour buffer on V3D; adopted from vkquake-perf.md) |
| `tools/gpu-lane/sdl2-drm/patches/0009-kmsdrm-submit-frame-before-waiting-for-previous-flip.patch` | the frame-pacing reorder (adopted from `patches-pace/0001`, frame-pacing.md) |
| `tools/gpu-lane/sdl2-drm/gamedrm/check-swap-order.sh` | objdump proof that an ELF links the 0009 order; run by build.sh, build-stk-drm.sh, relink-sdl-gl-game.sh, build-vkquake-drm.sh |
| `tools/gpu-lane/sdl2-drm/vkqdrm/vkqdrm_hooks.c` | loadso answers (phxvk), present counter (BSD-3) |
| `tools/gpu-lane/sdl2-drm/vkqdrm/gen-vk-trampolines.py` | the vk* link-symbol trampolines, generated per build |
| `tools/gpu-lane/sdl2-drm/vkqdrm/vkqdrm_compat.h`, `vkqdrm/include/execinfo.h` | the two libphoenix-gap bridges; self-retiring against `feat/ipv6mreq-execinfo`, delete after its merge (§3) |
| `tools/gpu-lane/sdl2-drm/vkqdrm/vkq-drm-launcher.c` | `/bin/vkq-drm` |
| `tools/gpu-lane/sdl2-drm/build.sh` | quakespasm-drm: links `gamedrm_hooks.c` + `--wrap=SDL_GL_SwapWindow`, swap-path proofs, new `--skip-sdl` (§6.4) |
| ~~`tools/gpu-lane/sdl2-drm/qsdrm/qsdrm_banner.c`~~ | deleted: `gamedrm_hooks.c` prints the same banner and sets the same SDL log levels |
| `tools/gpu-lane/xorg-drm/pi/startx-drm` | the new-lane X desktop launcher (`action`/`wmaker`, `HOLD`, `--servers`), §6.6 |
| `tools/gpu-lane/x11-drm/src/eglx11_demo.c` | USPosition/USSize WM hints from `-g`, `-t`/`XDEMO_TITLE`; built in `x11-drm/build-out-x` (§6.6) |
| `docs/gpu-new-lane/MIGRATION.md` | this document |
