# Quake III (quake3e) — Phoenix-RTOS port (GPL-2.0)

> **Engine build migrated to the ports framework.** The engine now builds from
> `sources/phoenix-rtos-ports/quake3/` (run `scripts/build-port.sh quake3`); the
> old `build-quake3e-phoenix.py` + `platform/` glue + `.patch` were removed and
> live there now as `glue/` + `patches/`. This directory retains only the
> non-migrated runtime launcher (`quake3-launcher.c`), the visual-regression
> capture reference demo (`demos/cap.dm_68`, used by
> `scripts/quake3-host-capture.sh`), and the GPL license (`COPYING`). The notes
> below are kept as bring-up history.

Phoenix-RTOS RPi4 port of [quake3e](https://github.com/ec-/quake3e) (Quake III
Arena engine), rendering through the ported Mesa/V3D GL stack over the SDL2
port. Like the QuakeSpasm, yQuake2 and vkQuake ports, this is an **optional
showcase**: opt-in, pulls the GPL quake3e source at build time, and the Phoenix
core does not depend on it.

**Pinned upstream commit:** `623982900a132e5c812dcb5231a430f28fafabeb`
(master, "Merge pull request #416"). Recorded in `build-quake3e-phoenix.py`
(`Q3_SHA`).

## What this is (Phase 1)

A single **static aarch64-phoenix ELF** that folds the client, the integrated
server, botlib, the `code/renderer` (opengl1) renderer and a Phoenix backend
into one binary and **links clean** (0 undefined symbols) against `libSDL2.a` +
`libGL-phoenix.a` + `libv3d-phoenix.a`. This is a link/bring-up milestone — no
Pi run and no game assets yet (Phase 2).

Build it:

```
scripts/build-sdl2-port.sh            # once, if .buildroot libSDL2.a is absent
python3 tools/quake3-port/build-quake3e-phoenix.py
# -> /tmp/quake3e-phoenix  (LINK OK)  ~27 MB, EXEC, statically linked
```

The upstream engine source is **not** vendored here. The build script compiles
this glue against a local quake3e clone (`external/quake3e/`, pinned to
`Q3_SHA`, not tracked) and applies `quake3e-phoenix-port.patch` to it
idempotently.

## Why Q3 is simpler than yQuake2 to fold into one ELF

yQuake2 had **two** dlopen seams to fold (game `.so` + renderer `.so`), each
needing static `GetGameAPI`/`GetRefAPI` plumbing and game-TU folding. Quake III
runs its game / cgame / ui modules as **interpreted QVM bytecode shipped in the
pak** (data, not C we compile or link), so:

- **No game TUs, no `GetGameAPI` plumbing.** The game logic is not in the
  binary at all.
- **`NO_VM_COMPILED`** makes `vm.c` force `VMI_BYTECODE` → the pure
  `vm_interpreted.c` interpreter. The `VMI_NATIVE` (`Sys_LoadDll` → dlopen) and
  the aarch64 JIT (`vm_aarch64.c`, `mmap(PROT_EXEC)` / W^X — unproven on
  Phoenix) paths are never compiled or reached.
- **`USE_RENDERER_DLOPEN=0`** compiles the renderer in; the engine calls its
  `GetRefAPI` directly (resolved at link time — see `nm` shows `T GetRefAPI`),
  so `Sys_LoadLibrary`/`Sys_LoadFunction` are never used for the renderer.

The Phoenix backend therefore only has to stub the dlopen seam (never called)
and supply `main` + a couple of libc/libm gap-fillers.

## Build config (mirrors the quake3e Makefile knobs)

`USE_RENDERER_DLOPEN=0 RENDERER_DEFAULT=opengl USE_SDL=1 USE_VULKAN=0
USE_CURL=0 USE_OGG_VORBIS=0 NO_VM_COMPILED`, bundled libjpeg. Defines:
`-DNO_VM_COMPILED -DUSE_OPENGL_API -DUSE_LOCAL_HEADERS=1` (and `-DBOTLIB` on the
botlib TUs only); **no** `-DUSE_RENDERER_DLOPEN / -DUSE_CURL / -DUSE_VULKAN_API
/ -DUSE_SYSTEM_JPEG`. `-fcommon` merges the tentative-definition globals the
modular build gives each TU independently.

## Phoenix backend (`platform/`)

- **`pl_phoenix_main.c`** — fork of `code/unix/unix_main.c`. Drops the unused
  legacy SysV/process headers (`<sys/ipc.h>`, `<sys/shm.h>`, `<sys/wait.h>`) and
  `<dlfcn.h>` (Phoenix has none; unix_main never referenced shmget/fork/wait).
  `main`, tty/console (`<termios.h>`), timing, signal registration are kept.
- **`pl_phoenix_sys.c`** — fork of `code/unix/unix_shared.c`. The four
  `Sys_LoadLibrary`/`Sys_UnloadLibrary`/`Sys_LoadFunction`/`Sys_LoadFunctionErrors`
  dlopen entry points become safe stubs (they log + fail if ever called; with
  QVM + static renderer nothing reaches them). Everything else — `Sys_Milliseconds`,
  file listing, mkdir, `Sys_FOpen`, home/pwd paths, affinity — is kept verbatim.
- **`pl_phoenix_stubs.c`** — the two symbols the engine + GL stack reference but
  Phoenix's libc/libm and the GPU archives lack: `rint` (libm subset gap, used
  by `common.c`) and `pthread_getcpuclockid` (Mesa `u_thread.c`).
- **`pl_phoenix_compat.h`** — force-included (`-include`) shim. Its sole job is
  to defuse the `msg_t` type clash (see below) **without a Q3-source rename**,
  plus supply `struct ipv6_mreq` (absent from Phoenix `<netinet/in.h>`, used by
  `net_ip.c`'s IPv6 multicast join).

`code/unix/linux_signals.c` is compiled unchanged (pure POSIX signal handling,
no dlopen).

## The `msg_t` clash — solved with zero Q3-source edits

Phoenix's socket header chain (`<netinet/in.h>` → `<sys/sockport.h>` →
`<sys/msg.h>` → `<phoenix/msg.h>`) defines a SysV-IPC `msg_t` that collides
head-on with Q3's network-buffer `msg_t` (`qcommon.h`) in every networking TU.
A blanket `-Dmsg_t=…` cannot disambiguate — it renames **both** typedefs to the
same name and just relocates the clash.

`pl_phoenix_compat.h` instead exploits `-include` running *before* the TU text:
it pre-parses the whole Phoenix socket/msg chain once with **Phoenix's** `msg_t`
renamed to a private name, tripping every include guard in that chain
(`_PH_MSG_H_`, `_LIBPHOENIX_MSG_H_`, and the transitively-parsed `sys/ioctl.h`
which also uses `msg_t.pid`). When a TU later includes `<netinet/in.h>` the
chain is a no-op, so **Q3's `msg_t` is the only one the TU ever sees**. Q3 never
calls `msgSend()`/`msgRecv()`, so the renamed Phoenix prototypes have no call
sites and are harmless. Result: no Q3-source rename across ~17 files.

## Source patch (`quake3e-phoenix-port.patch`, 3 files)

Applied in-place to the clone by the build script (idempotent):

- **`q_platform.h`** — add a `__phoenix__` OS branch (`OS_STRING "phoenix"`,
  `ID_INLINE inline`). `ARCH_STRING`/`Q3_LITTLE_ENDIAN`/`PATH_SEP`/`DLL_EXT`
  already resolve for `__aarch64__` in the common-unix block, so only the OS
  identity is missing; without it every TU dies at the `#error "Operating
  system not supported"` gate.
- **`renderer/qgl.h`** — (a) include `<GL/gl.h>` (from ported Mesa) for
  `__phoenix__` — but **not** `<GL/glx.h>`; (b) exclude the trailing X11/GLX
  proc-pointer block for `__phoenix__` exactly as it is already excluded for
  Apple. SDL owns the GL context (`SDL_GL_GetProcAddress`), and Phoenix has no
  `Display`/`GLXContext`/`XVisualInfo`. Grep confirms no `qglX*` reference
  anywhere in the renderer/sdl/client TUs.
- **`huffman.c`** — rename the file-local `send()` to `Huff_send()` so it no
  longer shadows POSIX `send()` (the compat shim force-includes the socket
  headers; the unqualified name collided). De-shadowing a libc name — an
  upstreamable cleanup.

## Renderer / audio

`code/renderer` (opengl1) only — never `renderer2` (GLSL, needs > GL 2.1) or
`renderervk` (needs `VK_KHR_swapchain` WSI, which V3DV lacks). opengl1 is
fixed-function immediate-mode GL 1.x, which our Mesa V3D 2.1 satisfies (same
path the QuakeSpasm port already renders on hardware). Sound is SDL-native
(`code/sdl/sdl_snd.c`); cURL / OGG-Vorbis / Vulkan are off.

## License

Derivative work of quake3e (Quake III Arena engine), **GPL-2.0** (see
`COPYING`), separate from the BSD-licensed Phoenix core. The GL-context glue it
links (`sources/phoenix-rtos-ports/sdl2/glue/sdl_phoenix_glctx.c`) carries a
GPL-consistent header. No GPL enters the Phoenix system repositories — this port
lives entirely in `external/` + `tools/`. The demo `pak0.pk3` (non-distributable
Quake III demo data) is **not** bundled or committed.

## Phase 2 (next, deferred until reliable storage)

- Acquire the Quake III **demo** `demoq3/pak0.pk3` (the demo installer unpacks
  into `demoq3/`, not `baseq3/`; the data is non-distributable — user-supplied),
  deploy to the NFS root; binary to `/usr/bin`.
- Netboot the Pi; launch `+set fs_game demoq3 +timedemo 1 +demo <name>`;
  HDMI-capture and compare vs host quake3e opengl1.
- **Infra caveat:** netboot NFS is 100 Mbps with runtime-read flakiness and no
  SD card is available, so game *runtime* testing is unreliable/slow right now
  (matches the Quake2 finding). Phase 1 (link) is the presently-verifiable
  deliverable.
