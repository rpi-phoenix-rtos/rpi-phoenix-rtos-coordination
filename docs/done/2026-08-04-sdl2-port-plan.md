# SDL2 port plan — Phoenix-RTOS RPi4 (task C1)

Feasibility analysis 2026-08-04. Underpins Quake2 (yQuake2), Quake3 (quake3e/ioq3),
SuperTuxKart. Scope: FULLSCREEN GL+Vulkan, mouse+keyboard, sound. No X11.

## Decision
Port **real upstream SDL 2.30.x** as `sources/phoenix-rtos-ports/sdl2/` (static
`libSDL2.a`) with custom "phoenix" internal drivers that call the SAME proven hardware
paths the Quake/vkQuake ports use. Do NOT extend the header-shim approach
(`tools/*/sdl-shim/SDL.h`) — it stubs out SDL's own subsystems, which yQuake2/quake3e/STK
consume too deeply to exclude. **SDL2 first** (all downstream games are SDL2-native);
SDL3 is a viable later track (driver code largely portable; same Vulkan-WSI problem).

## Port infrastructure
- Each port = `sources/phoenix-rtos-ports/<name>/{port.def.sh,patches/}`; driven by
  `sources/phoenix-rtos-build/build-ports.sh` → `port_manager.py`.
- **Mimic `zlib/port.def.sh`** — the existing CMake port (out-of-tree `build/`,
  `cmake … && make install` with the exported cross env). SDL2 has first-class CMake.
- No global CMake toolchain file exists — pass `-DCMAKE_C_COMPILER=$CC` + the threads
  override (below), or add a toolchain file.

## Driver mapping (reuse the referenced files)
- **Video** [M]: new `src/video/phoenix/` — fullscreen fb0 window, geometry via
  `RPI4FB_GETMODE`, hand scanout PA to V3D winsys; `FBCONSETMODE(FBCON_DISABLED)` on tty;
  reuse kbd0-release retry. Ref: `tools/quakespasm-port/platform/pl_phoenix_vid.c`.
- **OpenGL** [M]: NO EGL/GLX needed — implement the video driver's GL_* hooks over
  in-process Mesa (the `qsv3d_init` recipe). `GL_GetProcAddress` needs a **static
  name→fn table** (no dlopen/dlsym in libphoenix — confirmed). Ref:
  `tools/quakespasm-port/platform/pl_phoenix_glctx.c`.
- **Vulkan** [L, PHASE 2, risk #1]: V3DV has NO WSI (no VK_KHR_surface/swapchain). Real
  reusable SDL2 needs a **minimal VK_KHR_surface+swapchain implemented in V3DV** backed by
  the fb0 scanout page-flip (`vkQueuePresentKHR` = `v3d_phoenix_flip`; swapchain images =
  fb0-backed). Raw material: `tools/vkquake-port/platform/pl_phoenix_vk_vid.c`. Alt (app-side
  no-WSI shim) does NOT scale — rejected.
- **Input** [M]: `PumpEvents` drains `/dev/kbd0` (RAW 8-byte HID) + `/dev/mouse0` (4-byte),
  diffs → SDL events; HID usage → `SDL_Scancode` (rewrite of `pl_hid_key`). **poll() gotcha**:
  Phoenix poll() doesn't wake on HID fds → `SDL_WaitEvent` = pump-on-timer, never blocking
  poll; keep the bounded drain guard. Ref: `pl_phoenix_in.c`.
- **Audio** [S/M]: new `src/audio/phoenix/` PULL-model driver (`OpenDevice`/`WaitDevice`/
  `GetDeviceBuf`/`PlayDevice`) `write()`ing to `/dev/audio0` (blocking write = natural
  backpressure). Ref: `pl_phoenix_snd.c` (invert its push model).
- **Threads** [S, ready]: `src/thread/phoenix/` — lift `tools/vkquake-port/platform/
  pl_phoenix_sdlcompat.c` almost verbatim (mutex/cond/sem/thread over sys/threads.h,
  recursive-mutex emulation done). **Timers**: `CLOCK_MONOTONIC` confirmed in libphoenix →
  `SDL_GetTicks`/perf counter; `SDL_Delay`→usleep.

## Build (cross-configure probe already run)
- PASSES: C/C++ ABI, `CHECK_CPU_ARCHITECTURE_ARM64`, `HAVE_GCC_ATOMICS`, visibility. Benign
  fails: `__GLIBC__`, `immintrin.h`, altivec.
- **FIRST BLOCKER**: threads detection fails (`CMakeLists.txt:2989` "Threads … may not be
  disabled") — SDL's `find_package(Threads)` can't link-test libphoenix's libc-integrated
  pthreads (no separate `-lpthread`, static try-compile). Forcing `CMAKE_HAVE_LIBC_PTHREAD`/
  `CMAKE_THREAD_LIBS_INIT` did NOT suffice → needs a **patch** pinning `Threads_FOUND`/
  `SDL_THREAD_PTHREAD` (or a toolchain file providing `IMPORTED Threads::Threads`).
- Baseline flags: `-DSDL_SHARED=OFF -DSDL_STATIC=ON` + disable host backends
  (`-DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_KMSDRM=OFF -DSDL_PULSEAUDIO=OFF -DSDL_ALSA=OFF
  -DSDL_PIPEWIRE=OFF -DSDL_JACK=OFF -DSDL_OPENGLES=OFF`). Custom phoenix video/audio drivers
  wired via patch (SDL_config defines + driver dirs), not stock cmake switches.
- Expect further libphoenix compile-gap patches: `SDL_LoadObject`/dlopen (compile out —
  no dlfcn), `sysconf`/`_SC_*`, mmap flag coverage for fb0.

## Phased plan
**Phase 1 — fullscreen GL + input + audio (unblocks Quake2/3/STK):**
1. Create `sources/phoenix-rtos-ports/sdl2/` (port.def.sh mimic zlib; pin SDL 2.30.x). [S]
2. Threads-detection patch → clean cross-configure + static `libSDL2.a` link. [M]
3. `src/thread/phoenix/` (port pl_phoenix_sdlcompat.c) + MONOTONIC timer backend. [S]
4. `src/video/phoenix/` fb0 fullscreen + GL hooks + GL_GetProcAddress table + PumpEvents
   (kbd0/mouse0, pump-on-timer). [M–L]
5. `src/audio/phoenix/` pull-model over /dev/audio0. [S/M]
6. Validate on Pi4: SDL2 GL demo (fullscreen triangle + keypress + mouse + tone).

**Phase 2 — Vulkan**: minimal VK_KHR_surface+swapchain WSI in V3DV over fb0 page-flip,
then wire SDL_Vulkan_*. [L]

## Top risks
1. Vulkan WSI in V3DV (none exists; phase 2; most likely under-scoped).
2. CMake/libphoenix configure-build friction (threads gate proven; chain of small patches).
3. `GL_GetProcAddress` static table (no dynamic loader; grows with the GL surface the games use).

## Key file refs
- Port template: `sources/phoenix-rtos-ports/zlib/port.def.sh`; build: `sources/phoenix-rtos-build/build-ports.sh`, `port_manager.py`
- Thread backend to lift: `tools/vkquake-port/platform/pl_phoenix_sdlcompat.c`
- GL ctx: `tools/quakespasm-port/platform/pl_phoenix_glctx.c`; fb0 present: `pl_phoenix_vid.c`
- Input (HID keymap + poll gotcha): `pl_phoenix_in.c`; audio: `pl_phoenix_snd.c`
- Vulkan no-WSI (phase 2): `tools/vkquake-port/platform/pl_phoenix_vk_vid.c`
- Toolchain: `.toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc` (gcc 14.2.0)
