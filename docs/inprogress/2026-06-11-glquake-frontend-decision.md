# GLQuake frontend (Phase 4) — architecture decision (2026-06-11)

## Where we are
The ported Mesa v3d gallium driver renders on the Pi4 V3D from Phoenix and is
visible on HDMI (clear + triangle, readback- and snapshot-verified;
`rpi4-v3d-mesa`, manifests `2026-06-11-v3d-mesa-{renders-green,triangle}`). The GPU
question is fully answered. What remains for GLQuake is the **GL frontend**: an
OpenGL API on top of the working gallium `pipe_screen`/`pipe_context`, then
Quakespasm's GL renderer.

## The fork: st/mesa vs a light GL veneer
**Decision: st/mesa** (Mesa's OpenGL state tracker). Grounded in three spikes
(advisor-gated):

1. **Quakespasm GL usage (the deciding fact).** Cloned `external/quakespasm`
   (sezero mirror) and grepped: it uses **fixed-function immediate-mode GL
   extensively** — `glBegin`/`glEnd` in 11 files (gl_draw, gl_sky, r_part,
   gl_rmain, …), `glMatrixMode`/`glVertexPointer`/`glTexEnv`/`glLoadMatrixf`
   throughout. GLSL is an *optional* runtime path (`gl_glsl_able = false` by
   default — the ericw gamma/alias shaders). A "light veneer" of these calls would
   be reimplementing exactly what st/mesa already does (matrix stacks, texenv,
   immediate-mode vertex buffering, fixed-function→shader generation) — worse, not
   lighter. So st/mesa it is. Running Quakespasm fixed-function-only initially
   (keep `gl_glsl_able` false) likely avoids invoking the GLSL compiler at all.

2. **OSMesa absent.** `find external/mesa -iname '*osmesa*'` → nothing; gallium
   `frontends/` has only dri/glx/wgl/hgl/etc. (all windowing). So we **reconstruct
   a minimal off-screen frontend**: create an st context, wrap a gallium
   `pipe_resource` (our RT) as the GL framebuffer, render, read back, blit to
   `/dev/fb0` (the path `rpi4-v3d-mesa` already exercises). ~a few hundred lines,
   modeled on the classic osmesa.c.

3. **st/mesa cross-compiles + C++ risk is mitigated.** The host Mesa build's
   compile_commands already includes `src/mesa/{main:124, state_tracker:59}` +
   `src/compiler/glsl:79` — so the GL frontend is cross-compilable via the same
   link-drive method that built the v3d driver. The GLSL compiler is heavily C++
   (54/79 `.cpp`), BUT the toolchain HAS `aarch64-phoenix-g++` + `libstdc++.a` in
   the sysroot — so C++ is buildable, not a blocker (untested in our C-only flow,
   but available). st/mesa core + main are ~all C (2 `.cpp` each).

## Plan (Phase 4, the multi-week capstone — spikes first, no blind build)
1. **st/mesa link-drive spike** (next): extend `build-v3d-phoenix.py` to add
   `src/mesa/{main,state_tracker}` + the GL dispatch (`src/mapi`/`glapi`) on top of
   the existing v3d+gallium lib; link-drive a tiny GL smoke (`glClear` to an
   off-screen FB) → enumerate the closure + confirm whether the C++ GLSL compiler
   is pulled for the fixed-function path. Mirrors the v3d-driver de-risking.
2. **Minimal off-screen frontend**: st context + RT-backed GL framebuffer +
   makecurrent + flush→blit to `/dev/fb0`. Smoke: `glClear` then a GL triangle
   (`glBegin`) → HDMI. (st/mesa's fixed-function path generating the shaders Mesa
   already compiles to QPU — the reuse-Mesa win.)
3. **Quakespasm port**: replace its SDL GL-context backend (`gl_vidsdl.c`) with the
   off-screen frontend; build the Quake/ sources for aarch64-phoenix; stage
   `pak0.pak` (shareware) on the rootfs; wire USB-HID (kbd/mouse) for input;
   present each frame to `/dev/fb0`. Fixed-function-first (`gl_glsl_able`=false).

## De-risked
The GPU + driver + winsys + display path are all HW-proven. Phase 4 is GL-frontend
breadth (large but well-understood), not a question of whether Mesa can drive this
GPU. The link-drive method + the boot-launch/fast-UART loop carry straight over.
