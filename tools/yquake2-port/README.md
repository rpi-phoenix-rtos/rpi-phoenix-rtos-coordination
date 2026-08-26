# yQuake2 — Phoenix-RTOS port (GPL-2.0-or-later)

> **Engine build migrated to the ports framework.** The engine now builds from
> `sources/phoenix-rtos-ports/yquake2/` (run `scripts/build-port.sh yquake2`);
> the old `build-yquake2-phoenix.py` + `platform/` glue + `.patch` were removed
> and live there now as `glue/` + `patches/`. This directory retains only the
> non-migrated runtime launcher (`quake2-launcher.c`) and the GPL license
> (`COPYING`). The notes below are kept as bring-up history.

Phoenix-RTOS RPi4 port of [yQuake2](https://github.com/yquake2/yquake2)
(Quake II), rendering through the ported Mesa/V3D GL stack over the SDL2 port.
Like the QuakeSpasm and vkQuake ports, this is an **optional showcase**: it is
opt-in, pulls the GPL yQuake2 source at build time, and the Phoenix core does
not depend on it.

**Pinned upstream commit:** `e27fdcceb47769463b53b6d6f2e4c2ee572178b2`
(master, 2026-08-01, YQ2VERSION 8.71pre). Recorded in
`build-yquake2-phoenix.py` (`YQ2_SHA`).

## What this is (Phase 1)

A single **static aarch64-phoenix ELF** that folds the client, the baseq2
game logic, the `ref_gl1` renderer and a Phoenix backend into one binary and
**links clean** against `libSDL2.a` + `libGL-phoenix.a` + `libv3d-phoenix.a`.
This is a link/bring-up milestone — no Pi run and no game assets yet (Phase 2).

Build it:

```
scripts/build-sdl2-port.sh            # once, if .buildroot libSDL2.a is absent
python3 tools/yquake2-port/build-yquake2-phoenix.py
# -> /tmp/yquake2-phoenix  (LINK OK)
```

The upstream engine + game source is **not** vendored here. The build script
compiles this glue against a local yQuake2 clone (`external/yquake2/`, pinned
to `YQ2_SHA`, not tracked) and applies `yquake2-phoenix-port.patch` to it
idempotently.

## The central problem: dlopen → static single-ELF

Phoenix has no `dlopen`/`dlsym`. yQuake2 has two dynamic-load seams — the game
DLL (`Sys_GetGameAPI` → `dlopen("game.so")`) and the renderer DLL
(`Sys_LoadLibrary(..., "GetRefAPI", ...)` → `dlopen("ref_gl1.so")`). Both are
folded into one ELF by the Phoenix backend in `platform/`:

- **`pl_phoenix_sys.c`** — fork of `backends/unix/system.c`. `Sys_GetGameAPI`
  returns the compiled-in `GetGameAPI` directly; `Sys_LoadLibrary` returns the
  compiled-in `GetRefAPI`; `Sys_FreeLibrary`/`Sys_UnloadGame` are no-ops;
  `Sys_GetProcAddress` returns NULL (the GL renderer resolves procs through
  `SDL_GL_GetProcAddress`). `Sys_Realpath` uses a stack `PATH_MAX` buffer
  instead of the glibc `realpath(in, NULL)` allocating form libphoenix does
  not honour. Everything else (filesystem, time, dir walk, console) is kept.
- **`pl_phoenix_main.c`** — fork of `backends/unix/main.c` minus the Unix
  setuid sanity checks (`getuid()==0` would make Phoenix, where getuid() is 0,
  refuse to launch) and the `setenv("LC_ALL", ...)` locale pin.
- **`pl_phoenix_hunk.c`** — replaces `backends/unix/shared/hunk.c`. A plain
  `malloc(maxsize)`-backed hunk (no `mmap`/`mremap`, which Phoenix lacks);
  over-reserves with no shrink — fine for the demo/timedemo workload.
- **`pl_phoenix_glstubs.c`** — libc/Mesa gap-fillers ref_gl1 pulls from the GL
  stack that the SDL2 smoke test never did: `lroundf`,
  `pthread_getcpuclockid`.
- **`pl_phoenix_compat.h`** — force-included (`-include`) shim: pulls
  `<unistd.h>` globally (yQuake2 gates it per-OS, none of which match Phoenix,
  leaving `getcwd` &c. implicitly declared → a hard error under GCC 14),
  defines `struct ipv6_mreq` (absent from Phoenix `<netinet/in.h>`) and `MAX`
  (Phoenix `<sys/param.h>` lacks it).

The remaining changes are in `yquake2-phoenix-port.patch` (applied in-place to
the clone by the build script):

- **`vid.c`** `VID_HasRenderer` — reports the compiled-in `gl1` renderer as
  present instead of stat-ing a non-existent `ref_gl1.so`.
- **`refresh/files/common.c`** + **`game/g_main.c`** — drop the `.so`-era
  `Sys_Error`/`Com_Printf`/`Com_DPrintf`/`Com_Error` forwarders (they routed
  through `ri.*`/`gi.*` because a separate renderer/game `.so` could not see
  the engine's symbols). In one ELF they bind directly to the engine's real
  implementations; keeping them multiple-defines.
- **`cl_image.c`** — drop the second `STB_IMAGE_IMPLEMENTATION` (the renderer's
  `files/stb.c` already instantiates it; two copies collide in one binary).

## Single-ELF symbol hygiene (build flags)

- **`-fcommon`** merges the tentative-definition cvar globals each `.so`
  declares independently (`vid_fullscreen`, `vid_gamma`, `gl1_stereo*`,
  `maxclients`, `dedicated`, …) — correct here, since every copy is
  `Cvar_Get()`'d to the same cvar.
- **`-Dmodes=yq2_gl1_modes`** on the `ref_gl1` TUs renames its initialized
  `modes` table (GL texture filters) so it no longer collides with the
  client's initialized `modes` table (video-mode menu). Both are real
  definitions, so `-fcommon` cannot merge them; the rename is self-contained
  (no non-gl1 TU references the renderer's `modes`).

## Renderer / audio

`ref_gl1` only (never gl3/soft/gles — each defines `GetRefAPI` identically and
would collide). It is fixed-function immediate-mode GL 1.x, which our Mesa V3D
2.1 satisfies (same path the QuakeSpasm port already renders on hardware).
Sound is SDL-native (`client/sound/sdl.c`); OpenAL/cURL are left off.

## License

Derivative work of yQuake2 (Quake II), **GPL-2.0-or-later** (see `COPYING`),
separate from the BSD-licensed Phoenix core. The GL-context glue it links
(`sources/phoenix-rtos-ports/sdl2/glue/sdl_phoenix_glctx.c`) carries the same
GPL-consistent header. No GPL enters the Phoenix system repositories — this
port lives entirely in `external/` + `tools/`.

## Phase 2 (next)

- Acquire baseq2 assets (Quake II 2002 demo `pak0.pak` + `demo1.dm2`), deploy
  to NFS root (`/usr/share/quake2/baseq2/`), binary to `/usr/bin`.
- Netboot the Pi; launch `+set vid_renderer gl1 +playdemo demo1` /
  `+timedemo 1 +map demo1`; HDMI-capture and compare vs host yQuake2 gl1.
- Memory note: the malloc-backed hunk over-reserves each `Hunk_Begin(maxsize)`
  with no shrink; revisit if peak RSS matters on the Pi.
