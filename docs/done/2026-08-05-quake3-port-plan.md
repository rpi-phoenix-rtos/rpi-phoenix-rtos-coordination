# Quake III (quake3e) port plan — Phoenix-RTOS RPi4 (task C5)

Feasibility 2026-08-05. **Feasible, and structurally SIMPLER than Quake2**, because Q3's
game logic is interpreted **QVM bytecode (data, shipped in the pak)** — not compiled C —
so the yQuake2 "fold the game .so into the ELF" problem does not exist here. Built on the
HW-validated SDL2 port (task C1). Recommend **quake3e** (github.com/ec-/quake3e),
**`code/renderer` (opengl1) fixed-function renderer**, statically linked into ONE ELF, with
the game/cgame/ui modules run as **QVM via the pure interpreter**.

Evidence: ~15 representative TUs cross-compiled clean with the aarch64-phoenix toolchain
(see "Cross-build probe" below); a full undefined-symbol link closure is phase-1
implementation work, not yet done.

## PHASE 1 COMPLETE — link closure achieved (2026-08-05)

Single static aarch64-phoenix ELF links clean: **168/168 TUs compile, 0 undefined
symbols**, `EXEC` (statically linked, entry set), ~27 MB (`/tmp/quake3e-phoenix`).
`nm` shows `T GetRefAPI` / `T main` / `T VM_Create` resolved — confirming the
"no dlopen plumbing" thesis (renderer `GetRefAPI` binds at link time). Verifiable
without the Pi; reproducible from a pristine clone (build script auto-applies the
patch). **Pinned quake3e SHA: `623982900a132e5c812dcb5231a430f28fafabeb`.**

Deliverables in `tools/quake3-port/`: `build-quake3e-phoenix.py`,
`platform/{pl_phoenix_main,pl_phoenix_sys,pl_phoenix_stubs}.c` +
`pl_phoenix_compat.h`, `quake3e-phoenix-port.patch`, `README.md`, `COPYING`.

Reality vs the four predicted patch points:
1. `q_platform.h` Phoenix branch — as predicted (trivial).
2. `qgl.h` — GLX-block gate **plus** a needed `<GL/gl.h>` include for `__phoenix__`
   (the probe missed the GL-header branch; still small).
3. **`msg_t` clash solved with ZERO Q3-source rename** — the predicted "top risk /
   pervasive rename" was avoided. `pl_phoenix_compat.h` pre-parses the Phoenix
   socket/msg header chain under a private rename of *Phoenix's* `msg_t`, tripping
   its include guards so only Q3's `msg_t` is ever visible in-TU. This is the
   headline simplification over the plan.
4. Phoenix sys/net backend — as predicted; `net_ip.c` compiles from source
   unchanged (only needed `struct ipv6_mreq` from the compat shim).

Two items the probe did not surface, both minor: botlib TUs need `-DBOTLIB` (they
gate their include set on it), and `huffman.c`'s file-local `send()` shadowed
POSIX `send()` once the socket headers were force-included — renamed to
`Huff_send()` (a de-shadowing cleanup). libc/libm gaps: just `rint` and
`pthread_getcpuclockid` (both in `pl_phoenix_stubs.c`).

## quake3e vs ioq3 — pick quake3e
- **quake3e**: single self-contained `Makefile` with first-class knobs for exactly the
  config we need (`USE_RENDERER_DLOPEN=0`, `RENDERER_DEFAULT=opengl`, `USE_SDL=1`,
  `USE_VULKAN=0`, `USE_CURL=0`, `NO_VM_COMPILED`). Actively maintained, cleaner renderer.
- **ioq3**: current tree has **migrated to CMake** (no top-level Makefile in a fresh clone);
  same static-renderer capability but less convenient cross knobs. No advantage here.
Both share the same QVM/dlopen story; quake3e is the lower-friction target.

## Location
`external/quake3e/` (upstream clone, pinned SHA, not committed — like external/yquake2 /
external/vkquake) + `tools/quake3-port/` (platform glue + patches + `build-quake3e-
phoenix.py`), mirroring `tools/yquake2-port/`. Org-fork after HW-validation.

## Q1 — Renderer fits GL 2.1: `code/renderer` (opengl1) is pure fixed-function GL 1.x
quake3e ships three renderers; only the first fits our Mesa V3D (reports GL 2.1):
- **`code/renderer` (opengl1)** — SELECT THIS. Immediate-mode / vertex-array fixed function
  (`qglVertexPointer`/`qglTexCoordPointer`, `qglBegin`), **zero GLSL / `#version` /
  `glCreateShader` / VAO / core-profile** (grep count = 0). Multitexture/anisotropy/etc. are
  all `strstr`-probed optional extensions with graceful fallback (`tr_init.c`). Existence
  proof: our quakespasm port already renders immediate-mode GL 1.x on this exact V3D 2.1.
- `code/renderer2` (opengl2) — GLSL shader pipeline → needs > GL 2.1. **NO.**
- `code/renderervk` (vulkan) — needs VK_KHR_swapchain WSI, which V3DV lacks. **NO.**

## Q2 — QVM sidesteps dlopen entirely (the key simplifier)
Q3 loads game/cgame/ui as one of: native `.so` (dlopen), JIT-compiled QVM, or
interpreted QVM. `code/qcommon/vm.c`:
- `vm_game` / `vm_cgame` / `vm_ui` **all default to `"2"`** (`VMI_COMPILED`; enum is
  `VMI_NATIVE=0, VMI_BYTECODE=1, VMI_COMPILED=2`). Only `VMI_NATIVE` takes the `Sys_LoadDll`
  → dlopen path (`vm.c:1847,1853`); the default never does. **No native game `.so`, no
  dlopen for game logic.**
- The QVM bytecode (`vm/qagame.qvm`, `vm/cgame.qvm`, `vm/ui.qvm`) is **read from the pak** —
  confirmed present (`unzip -l pak0.pk3` → the three `vm/*.qvm`). Game logic is *data*, not
  C we compile or link.
- **Build with `NO_VM_COMPILED`** → `vm.c:1889` forces `VMI_BYTECODE` → `vm_interpreted.c`,
  the pure software interpreter. This deliberately avoids the aarch64 JIT
  (`vm_aarch64.c:2290,2341` = `mmap(PROT_WRITE)` + `mprotect(PROT_READ|PROT_EXEC)`), an
  unproven W^X / RWX-transition on Phoenix. Interpreted QVM is fully playable (id shipped Q3
  that way for years); JIT is a deferred perf lever, not a requirement.

**Static-link shape (ONE ELF, no game C):**
`code/client/*` + integrated `code/server/*` + `code/qcommon/*` (incl. `vm_interpreted.c`,
`unzip.c`) + `code/botlib/*` + `code/renderercommon/*` + `code/renderer/*` (opengl1) +
`code/sdl/*` (SDL2 client backend: glimp/input/snd) + **Phoenix sys/net backend** (fork of
`code/unix/`) + the SDL2 GL glue (`sdl_phoenix_glctx.c`) → link `--start-group libSDL2.a
libGL-phoenix.a libv3d-phoenix.a --end-group -lstdc++ -lm`. Contrast yQuake2: **no
`GetGameAPI`/`GetRefAPI` static-return plumbing and no game-TU folding needed** — the
renderer is one compiled-in `RE_*` refexport table (USE_RENDERER_DLOPEN=0) and the game is
QVM data.

## Q3 — Build system + patch points (all measured by cross-build probe)
Drive the link with a **`build-quake3e-phoenix.py`** mirroring
`tools/yquake2-port/build-yquake2-phoenix.py` (explicit TU lists + our custom cross flags +
our SDL2/GL archives), rather than quake3e's own Makefile — but the Makefile knobs above
document the intended TU/feature set. Cross flags mirror the yQuake2 recipe:
`-O2 -ffreestanding -fno-strict-aliasing -fcommon -Wno-error -DNDEBUG -DNO_VM_COMPILED`
+ the Phoenix platform defines + `-I code/qcommon -I code/renderercommon -I <SDL2/include>
-I <SDL2/include/SDL2> -I external/mesa/include`.

Four concrete patch points (found by probe, none large):
1. **`q_platform.h` Phoenix branch** [trivial] — add `OS_STRING "phoenix"`,
   `ID_INLINE inline`, `Q3_LITTLE_ENDIAN`, `DLL_EXT`. (`ARCH_STRING` already resolves for
   `__aarch64__`.) Without it every TU dies at the `#error "Operating system not
   supported"` gate.
2. **`code/renderer/qgl.h` GLX gate** [small] — the non-Win32/non-Apple branch
   (`qgl.h:271-296`) unconditionally declares X11/GLX procs (`QGL_LinX11_PROCS`,
   `glXSwapInterval*`) referencing `Display`/`GLXDrawable`/`XVisualInfo`, which Phoenix
   lacks. SDL provides the GL context (`SDL_GL_GetProcAddress`), so extend the `#ifndef
   __APPLE__` exclusion to Phoenix (or guard `#if !defined(USE_SDL)`). This is the ONLY
   thing blocking the opengl1 renderer TUs; renderercommon compiles clean as-is.
3. **`msg_t` type clash** [MODERATE — top risk, see below] — Phoenix's socket headers
   (`<netinet/in.h>` → `sys/sockport.h` → `sys/msg.h` → `<phoenix/msg.h>`) define a SysV-IPC
   `typedef struct _msg_t {...} msg_t;` that collides head-on with Q3's network-buffer
   `msg_t` (`qcommon.h:71`). This is a hard *type* clash (unlike yQuake2's tentative-global
   `modes` rename): a blanket `-Dmsg_t=...` renames both and does not disambiguate. Fix =
   **rename Q3's `msg_t` → `q3msg_t` in source** (mechanical but pervasive across
   qcommon/client/server), or a Phoenix net backend that avoids pulling `sys/msg.h` (harder;
   sockets need those headers). Only networking TUs hit it (`common.c` failed; the 8 other
   core TUs compiled clean).
4. **Phoenix sys/net backend** [low, mirror yQuake2] — fork `code/unix/unix_main.c` +
   `unix_net.c`: stub `Sys_LoadDll`/`Sys_LoadFunction`/`Sys_UnloadDll` (no `<dlfcn.h>` on
   Phoenix — but with QVM + static renderer these are never *called*), keep
   `main`/console/time/`Sys_Milliseconds`. Reuse yQuake2's `network.c` + `pl_phoenix_*`
   patterns. Set `USE_CURL=0` (curl is the only other dlopen seam, `cl_curl.c`).

**libc gaps: minimal.** No dlopen/dlsym needed anywhere in this config. Deeper gaps surface
only at link closure (phase-1 work); the SDL2 port already proved the libphoenix surface
(threads/timers/mmap/dirent/etc.).

## Q4 — Assets: Quake III **demo**, `demoq3/pak0.pk3` (NOT baseq3)
- The demo installer **`linuxq3ademo-1.11-6.x86`** unpacks `pak0.pk3` into a **`demoq3/`**
  folder (correcting the task's assumed `baseq3/` path). It contains q3dm0/q3dm1/q3dm7 + an
  intro demo + `vm/{qagame,cgame,ui}.qvm`.
- **Legal:** the demo is "gratis but not open source"; the game **data is
  non-distributable**. → freely downloadable for use, but **do NOT bundle/commit the pak**
  into the repo or image (same posture as the Quake1/Quake2 demo assets). The user
  downloads it and drops it at e.g. `/usr/share/quake3/demoq3/pak0.pk3` on the NFS root.
- **`pk3` = a ZIP** (confirmed: `Zip archive data ... deflate`), read via quake3e
  `code/qcommon/unzip.c` (bundled minizip) — contrast Quake2's proprietary `.pak`.
- Launch: point the basegame at demoq3 (`+set fs_game demoq3` / `fs_basegame demoq3`),
  `+set r_mode ...` for the fb0 geometry, `+timedemo 1 +demo <name>` for deterministic
  capture (reuse the Quake1 fixed-timestep harness pattern).

## Q5 — Phased plan + top risks
**Phase 1 — cross-build to one linking ELF (verifiable WITHOUT the Pi):** clone quake3e
(pin SHA); `tools/quake3-port/` glue + `build-quake3e-phoenix.py`; the four patches above
(q_platform.h, qgl.h GLX gate, msg_t rename, Phoenix sys/net backend); iterate undefined
symbols → 0. A clean single-ELF link is the milestone. ~3–5 focused days.

**Phase 2 — runtime/render (DEFERRED until reliable storage):** deploy binary + the
user-supplied `demoq3/pak0.pk3` to the NFS root; netboot the Pi; run `+timedemo`/`+demo`;
HDMI capture vs host quake3e opengl1.

**INFRA CAVEAT (blocks phase 2, not phase 1):** netboot NFS is 100 Mbps with runtime-read
flakiness and **no SD card is available**, so game *runtime* testing is unreliable/slow
right now (matches the Quake2 finding). Scope phase 1 as the only presently-verifiable
deliverable; do not assume render-testing is possible until reliable storage returns.

**Top risks:**
1. **`msg_t` type clash** — the one non-trivial patch; a mechanical but pervasive rename of
   Q3's `msg_t`. Everything else in the probe compiled clean.
2. **Runtime unverifiable now** — infra-bound (100 Mbps NFS + no SD). Phase 1 (link) is the
   deliverable; phase 2 waits on storage.
3. **QVM interpreter throughput** — acceptable (id shipped interpreted Q3); the aarch64 JIT
   is deferred because it needs `mprotect(PROT_EXEC)` (RWX), unproven on Phoenix.
4. **SDL2 completeness under Q3's usage** — quake3e's `code/sdl/*` backend leans on
   SDL_GL_*, relative-mouse, and the SDL audio callback; our phoenix SDL2 drivers must cover
   those (they were built for exactly this class of client).

## Cross-build probe (2026-08-05, evidence)
Toolchain `.toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc`, our `libSDL2.a` +
`external/mesa/include`, flags as above:
- Core qcommon: `vm.c`, `vm_interpreted.c`, `files.c`, `unzip.c`, `cvar.c`, `cmd.c`,
  `msg.c`, `net_chan.c` → **compile clean**. `common.c` → **only** the `msg_t` clash.
- `code/renderercommon/*` (tr_font, tr_image_png) → clean.
- `code/renderer/*` (opengl1: tr_init/tr_shade/tr_image/tr_main/tr_backend/tr_shader) →
  **only** the `qgl.h` GLX-block error (patch #2).
- `code/sdl/*` (sdl_glimp/sdl_input/sdl_snd) → **compile clean** against our SDL2 headers.
Conclusion: no libc dead-ends found; the port is gated by the four named patches, not by
missing platform primitives.

## Key refs
- Renderer GL fit: `code/renderer/tr_shade.c` (qglVertexPointer), `tr_init.c:326` (version).
- QVM/dlopen: `code/qcommon/vm.c:290-293` (vm_* default "2"), `:1847-1907` (interpret path),
  `qcommon.h:372` (enum), `vm_aarch64.c:2290,2341` (JIT RWX — avoided via NO_VM_COMPILED).
- Patches: `q_platform.h:152-227`, `renderer/qgl.h:271-296`, `qcommon.h:71` (`msg_t`),
  `code/unix/unix_main.c:42` (`<dlfcn.h>`), `cl_curl.c` (USE_CURL dlopen).
- Reuse recipe: `tools/yquake2-port/build-yquake2-phoenix.py` + `platform/pl_phoenix_sys.c`;
  SDL2 GL glue `sources/phoenix-rtos-ports/sdl2/glue/sdl_phoenix_glctx.c`;
  `docs/inprogress/2026-08-04-sdl2-port-plan.md`, `2026-08-05-quake2-port-plan.md`.
