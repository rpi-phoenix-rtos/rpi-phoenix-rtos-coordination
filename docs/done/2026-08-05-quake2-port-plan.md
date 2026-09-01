# Quake2 (yQuake2) port plan — Phoenix-RTOS RPi4 (task C4)

Feasibility 2026-08-05. **Feasible, ~5–8 focused days.** Built on the HW-validated SDL2
port. Recommend **yQuake2** (github.com/yquake2/yquake2, master, YQ2VERSION 8.71pre),
**ref_gl1** renderer, folded into a **single static ELF**.

## Location
`external/yquake2/` (upstream clone, local — not committed, like external/vkquake) +
`tools/yquake2-port/` (platform glue + `yquake2-phoenix-port.patch` + `build-yquake2-
phoenix.py`), mirroring `tools/quakespasm-port` / `tools/vkquake-port`. NOT a
phoenix-rtos-ports library port. Org-fork of the source can come once HW-validated (as
vkQuake did).

## Central problem SOLVED: dlopen → static single-ELF
Phoenix has no dlopen/dlsym (confirmed absent). yQuake2 has TWO dlopen seams:
1. Renderer load — `src/client/vid/vid.c:423` `Sys_LoadLibrary(...,"GetRefAPI",...)`.
2. Game logic — `src/server/sv_game.c:519` → `src/backends/unix/system.c:421 Sys_GetGameAPI` (`dlopen("game.so")`).
Both resolve to ONE exported symbol; `ref.h:128` already makes EXPORT/IMPORT empty. Fix =
a `src/backends/phoenix/` backend (fork of backends/unix/system.c) returning the
statically-linked symbols directly:
```c
extern refexport_t GetRefAPI(refimport_t);
extern game_export_t *GetGameAPI(game_import_t *);
void *Sys_LoadLibrary(const char *p, const char *sym, void **h){ *h=(void*)1;
    if(sym && !strcmp(sym,"GetRefAPI")) return (void*)GetRefAPI; return NULL; }
void *Sys_GetGameAPI(void *parms){ return GetGameAPI(parms); }
void  Sys_FreeLibrary(void *h){}
```
TWO extra patch points: (a) `vid.c:415 VID_HasRenderer()`/`VID_GetRendererLibPath()` do a
file-existence check on ref_gl1.so BEFORE load → patch to report the compiled-in renderer
present; (b) link EXACTLY ONE renderer (`GetRefAPI` is defined identically in gl1/gl3/sw
mains → collision) — take **ref_gl1 only**.

## Renderer: ref_gl1 fits GL 2.1
ref_gl1 is pure fixed-function immediate mode (glBegin/glVertexPointer/glTexCoordPointer),
no GLSL/VAOs/#version. Only hard gate `gl1_sdl.c:288` needs GL ≥1.4 → our V3D reports 2.1,
passes. All extensions optional (strstr-probed). Desktop-GL path links glXXX DIRECTLY
against libGL-phoenix.a (the GLAD proc-loader is only for the YQ2_GL1_GLES build we don't
select) → no dynamic GL-proc table. **Existence proof**: our quakespasm port already
renders immediate-mode GL 1.x on this exact Mesa V3D 2.1 over fb0. **ref_soft is NOT a free
fallback** — it blits via an SDL 2D renderer/texture, and our SDL2 is fullscreen-GL-only.

## Build → one ELF (mirror build-sdl2-gltest.py)
Fold client + baseq2 game + ref_gl1 + phoenix backend into ONE ELF (do NOT use yQuake2's
.so-building Makefile/CMake targets).
- Cross flags (all probed clean): `-O2 -ffreestanding -fno-strict-aliasing -Wno-error
  -DNDEBUG -DYQ2OSTYPE='"Phoenix"' -DYQ2ARCH='"aarch64"'`; do NOT define YQ2VERSION;
  GL via `-I external/mesa/include` (gl1 uses <GL/gl.h>).
- Link: `--start-group libSDL2.a libGL-phoenix.a libv3d-phoenix.a --end-group -lstdc++ -lm
  -Wl,-z,stack-size=33554432`.
- **Shared-TU dedup (3 copies!):** `common/shared/shared.c` + `md4.c` are compiled into
  client, game, AND renderer (CMakeLists ~350/481/562/619) → compile each ONCE, plus
  `backends/unix/shared/hunk.c` once, else multiple-definition at link.
- Hard-linked-game caveat: no dlclose/reload → game globals don't reset on map-change
  (irrelevant for demo/timedemo; matters for full campaign).

## libphoenix gaps — measured (nm over libc.a+libphoenix.a)
All DEFINED: opendir/readdir/closedir, realpath/getcwd/mkdir/stat/fstat/lstat, usleep/
clock_gettime/nanosleep/mmap/munmap, sigaction/signal/isatty/fnmatch/glob. Absent but
unused: scandir. Absent (the static-link point): dlopen/dlsym. Two caveats:
- `Sys_Realpath` uses `realpath(in,NULL)` (glibc alloc form, system.c:651) — libphoenix
  realpath may not honor NULL → patch to a stack PATH_MAX buffer.
- `hunk.c` uses `mmap(0,sz,...,MAP_PRIVATE|MAP_ANONYMOUS,...)` — verify Phoenix honors large
  MAP_ANONYMOUS, else swap to a malloc-backed hunk (Quake1 DEFAULT_MEMORY+touch pattern).
- Sound = SDL-native (client/sound/sdl.c) → reuse our SDL2 audio driver; OpenAL is
  USE_OPENAL-guarded, leave off.

## Assets
Quake II **2002 demo** (`q2-314-demo-x86.exe`) ships `baseq2/pak0.pak` + players/ + a
`demo1.dm2` (open mirrors; legal for a tech demo). Layout mirrors quakespasm:
`/usr/share/quake2/baseq2/pak0.pak` on NFS root; launch glue sets basedir. Demos play:
`+set vid_renderer gl1 +playdemo demo1` or `+timedemo 1 +map demo1`; apply the fixed-
timestep treatment from the Quake1 visual-regression harness for deterministic capture.

## Phases / risks
Phase 1 (~3–4d): external/yquake2 clone (pin SHA); tools/yquake2-port glue +
build-yquake2-phoenix.py; phoenix backend (static stubs + VID_HasRenderer patch +
Sys_Realpath guard + hunk decision); single-ELF link, iterate undefs → 0.
Phase 2 (~2–3d): deploy pak to NFS + binary to /usr/bin; netboot Pi; run playdemo/timedemo;
HDMI capture + compare vs host yQuake2 gl1.
Top risks: (1) dlopen→static plumbing (stubs + VID_HasRenderer gate + one-renderer rule);
(2) hunk.c anonymous-mmap (fallback malloc-hunk); (3) single-ELF symbol hygiene (3-way
shared.c/md4.c dedup + residual dup globals).

## Key refs
- dlopen seams: yquake2 src/client/vid/vid.c:415,423 · src/server/sv_game.c:519 ·
  src/backends/unix/system.c:421,697. GL gate: gl1_sdl.c:288; exports gl1_main.c:2109 /
  g_main.c:113. Shared dup: CMakeLists ~350/481/562/619.
- Reuse recipe: tools/sdl2-port/build-sdl2-gltest.py · sources/phoenix-rtos-ports/sdl2/glue/
  sdl_phoenix_glctx.c · pattern tools/quakespasm-port. Toolchain: .toolchain/.../aarch64-phoenix-gcc.
