# Quakespasm → Phoenix (V3D / libGL-phoenix) port plan

Status: **scoped, ready to implement** (2026-06-12). The GPU foundation is complete
(see `project_pi4_v3d_scout` memory UPDATEs through 25az): the Mesa GL frontend on V3D
renders immediate-mode `glBegin/glEnd`, `GL_QUADS`, depth test, perspective, multi-axis
rotation, and **fullscreen native 1024×768** (the viewport gate is cleared). Next: drive
a real game — Quakespasm — through that stack.

## Source & layout

- Upstream source already local: `external/quakespasm/Quake/` (~50 translation units).
- Object groups (from its `Makefile`):
  - **GLOBJS** (renderer, ~19): `gl_refrag gl_rlight gl_rmain gl_fog gl_rmisc r_part
    r_world gl_screen gl_sky gl_warp gl_draw image gl_texmgr gl_mesh r_sprite r_alias
    r_brush gl_model` (+ the SDL vid file below).
  - **Engine core** (~30, portable C): `strlcat strlcpy net_* chase cl_* console keys
    menu sbar view wad cmd common miniz crc cvar cfgfile host host_cmd mathlib pr_*
    sv_* world zone` + `snd_dma snd_mix snd_mem bgmusic`.
  - **SDL platform layer** (the files to REPLACE with Phoenix shims): `gl_vidsdl.c`,
    `in_sdl.c`, `snd_sdl.c`, `cd_sdl.c` (→ use `cd_null.c`), `sys_sdl_unix.c` +
    `pl_linux.c`, `main_sdl.c`.

## Toolchain (confirmed working)

- `./.toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc` (GCC 14.2.0) compiles Quake C.
- AR: `aarch64-phoenix-gcc-ar`. Link against `/tmp/libGL-phoenix.a` + `/tmp/libv3d-phoenix.a`
  (`-Wl,--start-group ... --end-group -lstdc++ -lm`), as `rpi4-glcube` does.

## The gating dependency: an SDL2 header+impl shim

Every TU includes `quakedef.h`, which (lines ~229-245) pulls `<GL/gl.h>` + `SDL2/SDL.h`
+ `SDL2/SDL_opengl.h`. So nothing compiles without an SDL2 shim. This is the bulk of the
port — a thin SDL2 mapped onto Phoenix:

| SDL surface Quake uses | Phoenix backing |
|---|---|
| `SDL_Init/Quit`, `SDL_GetTicks`, `SDL_Delay` | clock + `usleep` |
| `SDL_CreateWindow`, `SDL_GL_CreateContext`, `SDL_GL_SwapWindow` | surfaceless `st_create_context` + FBO (reuse the `rpi4-glcube` pattern); SwapWindow = `glReadPixels` → blit to `/dev/fb0` (native 1024×768 now works — continuous re-blit beats fbcon klog) |
| `SDL_GL_GetProcAddress` | return our libGL-phoenix function pointers (base GL 1.x) or NULL for unsupported extensions |
| `SDL_PollEvent` keyboard/mouse | read `/dev/kbd0` (HID works) → translate to Quake keys; mouse later |
| `SDL_OpenAudio` / audio callback | **stub/no-op first** (silent); real audio is a later tier |
| `SDL_GetWindowSize` etc. | fixed 1024×768 |

Headers needed: a minimal `SDL.h` + `SDL_opengl.h` shim providing the types/enums/protos
Quake references (`SDL_Event`, `SDL_Window`, `SDLK_*`, `SDL_Scancode`, `Uint32`, …). Scope
the exact symbol set by compiling and chasing errors.

## GL strategy (low risk — foundation proven)

- Base **GL 1.x** (`glBegin/glEnd`, `glDrawArrays`, `glTexImage2D`, `glTexCoord`, matrix
  stack, depth, blend) comes straight from `libGL-phoenix` — all exercised by the cube.
- **Extensions** (multitexture/lightmaps `glActiveTexture/glMultiTexCoord`, and the modern
  GLSL path) are loaded by Quake at runtime via `GL_GetProcAddress`. The multitexture
  entry points ARE exported by libGL-phoenix (verified). Lightmaps (two-texture blend) are
  the biggest *untested* GL feature — retire that risk with a standalone 2-texture probe,
  or let Quake be the test.
- Force the **legacy fixed-function renderer** initially (`r_glsl 0` / avoid the GLSL
  program path) to lean on the proven immediate-mode surface; enable GLSL later.

## Known forward risks

- **Winsys never frees GPU VA** (`DRM_V3D_DESTROY_BO` is a no-op; `next_gpuva` only bumps).
  Fine for the cube; Quake's textures/lightmaps are persistent BOs, so a level *load* or
  *transition* accumulates VA. 128 MiB likely holds one level, maybe not map changes. The
  bounds guard (25az) turns this into a loud `-ENOMEM`, not corruption — but implement VA
  reclaim (a free-list, or honor DESTROY_BO) *before* relying on multi-level play.
- **Perf:** EZ is disabled (25ay) → full overdraw shading; fullscreen runs ~10 fps for the
  cube. Quake's world is heavier. Acceptable for first-light; revisit (re-enable EZ /
  enable caches TD-16) for playability.
- **y-flip:** cosmetic (immediate-mode Quake won't care); ignore for now.

## Implementation order (each a build/validate checkpoint)

1. `tools/quakespasm-port/` + `build-quakespasm-phoenix.py` (model on `build-gl-phoenix.py`):
   compile flags, include dirs (`external/mesa/include` for GL, `external/quakespasm/Quake`,
   the SDL shim dir), force-includes.
2. Minimal SDL2 shim **headers** → get the **engine core** + **GLOBJS** to *compile*
   (chase the symbol set). No real impl yet — link-enumerate to get the gap list.
3. SDL2 shim **impl** (vid/input/timing/sys) backed by FBO/kbd0/clock; audio stub.
4. Link the full `quakespasm` ELF against libGL-phoenix + libv3d-phoenix → resolve the
   remaining gaps (the closure-reduction move that worked for the GL frontend, 114→0).
5. Stage shareware `pak0.pak` (~18 MB) on the NFS/SD rootfs; boot/psh-launch; HDMI snapshot
   of e1m1 attract mode (no input) = the ship line.
6. Wire `/dev/kbd0` input → playable; mouse; then audio/perf tiers.

## Validation (unattended)

Boot- or psh-launched (GPU access is fully in-process, no daemon). UART self-log + auto
HDMI snapshots (`artifacts/hdmi/*.png`) — read the PNGs for the rendered frame, exactly as
for the cube. Full-frame checks, never center-pixel (the 25ay lesson).
