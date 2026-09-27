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
| **X desktop** (`startx_gpu action`) | `/bin/startx_gpu` = `pl_phoenix_xlaunch`: starts `/sbin/rpi4-v3d` (old GPU daemon), `Xphoenix-glamor-daemon` (kdrive fbdev DDX + glamor shim, damage bands `glReadPixels`'d to `/dev/fb0`), then Window Maker + `gl-x11-window-daemon` + 2 × xterm (Life in CPython, top) + xbill + xclock | `Xorg-drm` / `Xorg-drm-m4p2` (xorg-server 21.1.24 hw/xfree86 + modesetting + glamor on GBM/EGL + DRI3/Present + `phxhid` input) | 🟡 [Pi] xclock (m4c), Window Maker desktop (m4d), DRI3/Present GL client **485 fps / 60.00 vsynced** (m4p2a) | (a) an **`action` launcher for Xorg-drm** (the xlaunch layout, one script: servers → shmsrv → Xorg-drm → wmaker + clients; `xorg-drm/pi/xorg-drm-m4a.sh` runs one client only); (b) the GL window: `gl-x11-window-daemon` is an old-lane harness (gallium internals + `XPutImage`) — replace it with `x11-drm`'s `eglx11-demo` (EGL on X11 via DRI3/Present, needs a `-geometry`-style placement option) or a Mesa-DRM `--x11` relink of a GLX/EGL demo; (c) `phxhid` input never exercised on the Pi (the gate needs none); (d) `-C` console handover untested. **Not needed:** page flips of client buffers (G7; Present falls back to a copy), G4, G6 |
| X clients (wmaker, xterm, xclock, xbill, xcalc, dillo, …) | plain X11 clients of `Xphoenix` | the same binaries on `Xorg-drm` (m4c ran the old-lane `xclock`, m4d the old-lane `wmaker`) | ✅ [Pi] | none — no GPU code in them |
| **HEVC player** `/bin/hevc-play` | rpivid decode → `write()` to `/dev/fb0` | — | ⬜ | port to a KMS dumb buffer + plane (`drmModeAddFB` + atomic/`SETCRTC`), or keep on fbdev emulation (§4). Zero-copy of decoder frames needs **G7** (kms import of a foreign buffer) |
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
  `yquake2-drm` + `.map` for addr2line, `quake2-drm` (`b08ee6a4…`), `BUILD-INFO.txt`.

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
  (`0f1045c2…`).

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
  first cycle, candidate for removal after one A/B). Plus one **new** patch, 0005: no timestamp query
  pool on Phoenix — vkQuake records `vkCmdResetQueryPool` + 2 × `vkCmdWriteTimestamp` every frame, v3dv
  runs both as CPU jobs through `DRM_IOCTL_V3D_SUBMIT_CPU` (the server advertises the CPU queue, which v3dv
  requires, but answers `SUBMIT_CPU` with `-ENOSYS` — gap **G5**), so the first frame would lose the
  device [read `v3dv_queue.c` `handle_reset_query_cpu_job`, `v3da_main.c`]; the pool only feeds
  `scr_speeds`' GPU time. Remove 0005 when the server serves `SUBMIT_CPU`. **Dropped** (fb0-shim only): `PL_VkHostAllocator`
  for shader modules, the de-static'd pipeline helpers, 2D `CULL_MODE_NONE`, `SCR_DrawGUI` canvas removal,
  the demo-loop arming, the alias alpha=1 hunks (the display plane is XRGB8888: alpha is ignored). SPIR-V:
  the port's vendored `glue/vkquake_shaders.c` (this commit's shaders; the alias-alpha shader hunk tests a
  ubo flag bit only the dropped `r_alias.c` hunk sets, so it is inert).
* **SDL:** a second build of the sdl2-drm SDL tree (same patches 0001–0008 + overlay) with
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
  `vkq-drm` (`aaf70271…`), `vk-direct-calls.txt`, `gl-stub-names.txt`, `BUILD-INFO.txt`.

## 3. Remaining blockers, by user

| Blocker | Affects | State | Needed for migration? |
|---|---|---|---|
| Pi cycles of the three clones | q2, q3, vkq | pre-registered §6 | **yes** |
| `flipstat` in quakespasm-drm | the gate's `frames` column | ✅ [built] 2026-09-27: `sdl2-drm/build.sh` links the shared `gamedrm/gamedrm_hooks.c` (`-DGAMEDRM_NAME='"quakespasm-drm"' -DGAMEDRM_API='"desktop GL"'`, replacing `qsdrm/qsdrm_banner.c`, whose banner and SDL log levels it reproduces byte for byte) and `-Wl,--wrap=SDL_GL_SwapWindow`; objdump `GL_EndRendering → b __wrap_SDL_GL_SwapWindow → bl SDL_GL_SwapWindow`, one direct call of the real swap (the wrapper's). Pi check: `mig-qs` (§6.4) | **yes** (else the gate fails mechanically) — done pending `mig-qs` |
| libphoenix gaps `struct ipv6_mreq` + `<execinfo.h>` | vkquake-drm (bridged in `vkqdrm/`), and the yquake2/quake3/vkquake ports (own `ipv6_mreq` copies) | ✅ [built] on branches `feat/ipv6mreq-execinfo` (pushed to `publish`, **not merged**): libphoenix `17c4fae` (`ipv6_mreq` + `IPV6_ADD/DROP_MEMBERSHIP`; `IPV6_JOIN_GROUP`/`LEAVE_GROUP`/`V6ONLY` renumbered to lwip's 12/13/27 — lwip receives optname unchanged) + `62e76b8` (`backtrace()` = aarch64 frame-record walk, 0 frames elsewhere; `backtrace_symbols[_fd]` = `0x<hex>`, one block); phoenix-rtos-tests `a8f2d6b` (`test-libc-execinfo`, `misc/netinet_in.c`); phoenix-rtos-ports `35abace` (the three ports' copies skip themselves when `IPV6_ADD_MEMBERSHIP` is defined — without it the libphoenix merge breaks their builds: a second `struct ipv6_mreq` is an error under gnu11/gnu17). On rpi4b lwip is built without IPv6, so `IPPROTO_IPV6` options stay ENOPROTOOPT whatever the number | no (the bridges work). **Merge order:** ports `35abace` first (or together), then libphoenix, then tests. **After the libphoenix merge, delete:** `tools/gpu-lane/sdl2-drm/vkqdrm/include/execinfo.h` and the `ipv6_mreq` block of `vkqdrm/vkqdrm_compat.h` (+ its `-idirafter` in `build-vkquake-drm.sh`), and the three ports' copies (`yquake2`/`quake3` `glue/pl_phoenix_compat.h`, `vkquake/glue/vkq_phoenix_compat.h`) |
| X `action` launcher on Xorg-drm + a GL window client | X desktop | not written | **yes** |
| Shader disk cache in Mesa-DRM | every GL/Vulkan app: cold shader compiles at every start (STK loads at < 1 fps for a while) | not built | no (startup time only); wanted before shipping |
| libphoenix `fclose(stdout)` UAF fix | STK exit fault (old and new lane) | branch `fix/stdstream-fclose-uaf` | yes for a 0-fault gate (the fault is at exit, inside the capture) |
| **G4** render-node export (`V3DA_OP_BO_EXPORT` + `/v3dbuf`) | UIF client buffers in X, Wayland dmabuf, v3dv external memory | implemented 2026-09-27 (`52f039791`), pending Pi `m6g-g4` ([M6 §15](M6-wayland.md)) | no (DRI3 with `dmabuf_capable` off works — m4p2a) |
| **G6** cross-process syncobj / sync-file fds | DRI3 1.4 explicit sync, Vulkan **xcb** WSI | open | no (no shipped Vulkan-in-X user) |
| **G7** kms import of a foreign buffer (+ `BO_LAST_FENCE`) | Present flips of client buffers, zero-copy HEVC | open | no (copy fallback) |
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
   0001–0008` + overlay: Phoenix audio + HID), `SDL_VULKAN=ON` with `patches-sdl-vulkan/0001` (costs
   nothing for GL users: the Vulkan code needs no link dependency). The `/dev/fb0` video backend
   (`SDL_phoenixvideo.c`, `PHOENIX_*`), `sdl2/glue/sdl_phoenix_glctx.c` and `sdl_phoenix_glstubs.c` are
   deleted.
4. **Game ports** (quakespasm, yquake2, quake3, supertuxkart): link the new stack instead of
   `libSDL2.a(old) + libGL-phoenix + libv3d-phoenix + glue` — exactly the `gamedrm/relink-sdl-gl-game.sh` /
   `build-stk-drm.sh` substitution, moved into each `p_build` (GLES shape for yquake2/STK, desktop-GL
   bridge shape for quakespasm/quake3). The `external/mesa/include` include path becomes the mesa-drm
   headers. Their engine patches stay.
5. **ports/vkquake** is rewritten: upstream TU list, patches-vkquake 0001–0005 instead of the fb0 patch (0005
   only until G5),
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
| `x` | `startx_gpu action` | the Xorg-drm `action` launcher (to write, §3) | Window Maker + GL window animating + both xterms + xbill + xclock, the xlaunch layout |
| `qspasm` | `quakespasm` | `/usr/bin/quakespasm-drm` (with a flipstat relink) | the attract demo renders (lit, textured, HUD) |
| `q3` | `/usr/bin/quake3 +map q3dm1` | `/usr/bin/quake3-drm +map q3dm1` | q3dm1 lit, textured (lightmaps), not the main menu |
| `q2` | `/usr/bin/quake2` | `/usr/bin/quake2-drm` | demo1 in full textured 3D |
| `vkq` | `vkquake` | `/bin/vkq-drm` | start map; `check-torch-rois.py --label <label>-vkq` PASS (≥ 2 frames, both archway torches lit, viewpoint MAE < 8) |
| `stk` | `stk --track=hacienda --numkarts=4 --profile-laps=2` | `/bin/stk-drm --track=hacienda --numkarts=4 --profile-laps=2` | lit hacienda race, 4 karts, HUD |

**Pass:** 6/6 apps with `prompt=yes`, `faults=0`, `frames > 0`, the HDMI check by eye (the script's own
warning: mechanical only), torches PRESENT — on the post-migration image, with `grep -a -c` of the
old-lane strings (`v3d-winsys:`, `/dev/fb0` in GPU apps, `phxgl`, `V3DV_PHOENIX`) = 0 over the rootfs. The
fps of each app is recorded against the last old-lane gate (it is not a pass criterion, but a regression
beyond the vsync quantisation — 60/n on the new lane — is a finding to explain before deleting).

## 6. Pre-registered Pi cycles — the three new clones (+ `mig-qs`)

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

**Stage (coordinator)** (`sudo install -m 755 <source> <path>`, then `cmp`; `<export>` = the live fsid=0
export, `awk '!/^#/ && /fsid=0/{print $1; exit}' /etc/exports`), sources under
`tools/gpu-lane/sdl2-drm/build-out/`:

| Source | Export path |
|---|---|
| `quake2-drm/yquake2-drm.stripped` | `<export>/usr/bin/yquake2-drm` |
| `quake2-drm/quake2-drm` | `<export>/usr/bin/quake2-drm` |
| `quake3-drm/quake3e-drm.stripped` | `<export>/usr/bin/quake3e-drm` |
| `quake3-drm/quake3-drm` | `<export>/usr/bin/quake3-drm` |
| `vkquake-drm/vkquake-drm.stripped` | `<export>/usr/bin/vkquake-drm` |
| `vkquake-drm/vkq-drm` | `<export>/bin/vkq-drm` |
| `quakespasm-drm.stripped` (§6.4; replaces the m3p4/poll-wake copy) | `<export>/usr/bin/quakespasm-drm` |

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
| `tools/gpu-lane/sdl2-drm/patches-vkquake/0001–0005` | the port's four non-video engine fixes, split per file, + 0005 (no timestamp queries until G5) |
| `tools/gpu-lane/sdl2-drm/vkqdrm/vkqdrm_hooks.c` | loadso answers (phxvk), present counter (BSD-3) |
| `tools/gpu-lane/sdl2-drm/vkqdrm/gen-vk-trampolines.py` | the vk* link-symbol trampolines, generated per build |
| `tools/gpu-lane/sdl2-drm/vkqdrm/vkqdrm_compat.h`, `vkqdrm/include/execinfo.h` | the two libphoenix-gap bridges; self-retiring against `feat/ipv6mreq-execinfo`, delete after its merge (§3) |
| `tools/gpu-lane/sdl2-drm/vkqdrm/vkq-drm-launcher.c` | `/bin/vkq-drm` |
| `tools/gpu-lane/sdl2-drm/build.sh` | quakespasm-drm: links `gamedrm_hooks.c` + `--wrap=SDL_GL_SwapWindow`, swap-path proofs, new `--skip-sdl` (§6.4) |
| ~~`tools/gpu-lane/sdl2-drm/qsdrm/qsdrm_banner.c`~~ | deleted: `gamedrm_hooks.c` prints the same banner and sets the same SDL log levels |
| `docs/gpu-new-lane/MIGRATION.md` | this document |
