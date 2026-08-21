# E5 GPU-parity: glamor-on-V3D feasibility + staged plan (2026-08-21)

Source-grounded investigation (3 parallel read-only subagents over the local
xserver-1.20.14 glamor source, the Phoenix x11-port, and the v3d-driver-port
winsys). Discharges the E5/G-XORG-MODERN "investigate HOW to reach Linux/RPi-OS
GPU-accelerated-X parity" step. **No code changed — this is the plan for the
attended/dedicated implementation thrust.**

## Verdict: FEASIBLE with no new GPU, GL, or DRM capability

Accelerated 2D X (glamor) on our V3D 4.2 is achievable **in-process, with no EGL,
no GBM, and no kernel DRM/KMS**. The long-standing "true glamor BLOCKED" note was
about the EGL-on-GBM-on-DRM plumbing — and that plumbing is **not required** for
the core glamor render path. Both halves of the bridge already exist; the work is
a thin shim between them, not new capability.

### Half 1 — glamor's core is decoupled from EGL/GBM/DRM

- All EGL/GBM/DRM lives in `glamor_egl.c`, a **separate Xorg-only module** (built
  under `hw/xfree86/glamor_egl/`), NOT in `libglamor.la`
  (`glamor/Makefile:631-647`). With `GLAMOR_HAS_GBM` undefined the core library
  compiles with **zero** EGL/GBM/DRM symbols — `epoxy/egl.h` is included only under
  `#ifdef GLAMOR_HAS_GBM` (`glamor_priv.h:41-45`), and the Phoenix dix-config has
  `GLAMOR_HAS_GBM`, `DRI3`, `GLXEXT`, `GBM_*` all undefined (`include/dix-config.h`).
- `glamor_render.c` / `glamor_copy.c` / `glamor_core.c` / `glamor_fbo.c` /
  `glamor_rects.c` contain **zero** egl/gbm/drm references. Render/Copy/Fill need
  only a current GL context.
- **The one hard requirement:** a populated `struct glamor_context`
  (`glamor_context.h:33-47`) whose `make_current` callback makes our GL context
  current. It is installed by `glamor_egl_screen_init` — but the shipped stub
  (`glamor_egl_stubs.c:33-36`) is **empty**, so `glamor_make_current`
  (`glamor_utils.h:727-734`) would call a NULL fn-ptr. **Make-or-break task = a
  ~10-line non-empty `glamor_egl_screen_init`** that sets `ctx->ctx`,
  `ctx->display`, `ctx->make_current`, modeled on `glamor_glx_screen_init`
  (`glamor_glx.c:53-67`, the existing "adopt an already-current context" path).
- GL feature gates `glamor_init` gates on (`glamor.c:571-613`): GL ≥ 2.1,
  `GL_ARB_texture_border_clamp`, `GL_ARB_fragment_program` (for <GL3.0), VAO
  (`ARB/OES_vertex_array_object`), readable GLSL version. `ARB_map_buffer_range`
  is optional (soft fallback). The four `glamor_egl_fd*`/`fd_name` exporters can
  keep their stub `-1`/`0` bodies — that only disables DRI3/Present *client* buffer
  sharing, not rendering.

### Half 2 — we already have in-process GL 2.1 + offscreen FBO (proven)

- Phoenix userspace obtains a **current GL 2.1 context in-process** via the Gallium
  frontend path — NOT EGL, NOT classic DRI, NOT bespoke:
  `v3d_phoenix_powerOn()` → `v3d_screen_create()` → `pscreen->context_create()` →
  `st_create_context(API_OPENGL_COMPAT, …)` → `_mesa_make_current(ctx, NULL, NULL)`
  (surfaceless). Reusable glue: `phxgl_init` / `phxgl_make_current` in
  `sources/phoenix-rtos-ports/sdl2/glue/sdl_phoenix_glctx.c:168-190`.
- **Offscreen-FBO + readback with no scanout is already proven**:
  `tools/x11-port/gl_x11_window.c` is a working "GPU in an X window" ELF — renders
  into a DRAM RGBA8+DEPTH24 FBO, `glReadPixels`, `XPutImage` into an Xphoenix
  window (`:160-293`), reporting `GL 2.1 Mesa 26.2.0 / V3D 4.2`. It uses exactly the
  winsys API above and nothing else (no EGL/GBM/DRM).
- "No kernel DRM" precisely: Mesa's v3d driver still issues the V3D **DRM ioctl
  ABI**, but `drmIoctl` is an **in-process libdrm shim**
  (`tools/v3d-driver-port/shim-include/xf86drm.h` → `v3d_libdrm_shim.c` →
  `v3d_phoenix_winsys.c:1598` → direct MMIO). No `/dev/dri`, no `libdrm.so`, no GBM,
  no EGL.
- Zero-copy option: `st_context_teximage(st, GL_TEXTURE_2D, …, pipe_resource, …)`
  wraps a Gallium resource as a GL texture (`gl_frontend_smoke.c:61-74`) — lets a
  glamor pixmap be a GL texture directly, avoiding `glReadPixels`.

## Integration architecture (glamor + kdrive fbdev DDX + present-to-/dev/fb0)

Xphoenix today is a **static** kdrive `fbdev` DDX (shadow-FB + `write()` to
`/dev/fb0`; no dlopen). Target design, minimal delta:

1. Compile `libglamor.la` into Xphoenix (static), `GLAMOR_HAS_GBM` undefined; link
   `tools/.gpu-libs/libGL-phoenix.a` + `libv3d-phoenix.a`.
2. Non-empty `glamor_egl_screen_init` installs the phxgl context + a `make_current`
   that calls `phxgl_make_current` (the make-or-break shim).
3. Screen/root pixmap backed by a glamor GL texture; Render ops run on the GPU.
4. **Present:** on damage/flush, read the screen pixmap back and `write()` to
   `/dev/fb0` (same present model as today, GPU-rendered source) — or, later,
   zero-copy via `st_context_teximage`/scanout page-flip
   (`v3d_phoenix_set_next_scanout`/`v3d_phoenix_flip`).

## Constraints (real, but not blockers for X-as-sole-GPU-owner)

- **Single GPU process.** V3D 4.2 is single-context HW and the winsys is a
  per-process singleton (`v3d_phoenix_winsys.c:209-237`; `g_st` singleton in
  `sdl_phoenix_glctx.c:53`; SDL2 refuses a 2nd context). Xphoenix can own the GPU
  fine, but cannot run **concurrently** with a separate GPU process (e.g. GLQuake)
  until a v3d-server time-slicing daemon exists. Acceptable for an accelerated
  desktop; document it.
- **Static link only** (no dlopen) — glamor is compiled in, not a loadable module.
  Fine: `libglamor.la` is a static convenience lib.
- glamor is deeply tied to the X `ScreenPtr`/`PixmapPtr`, so there is **no
  standalone glamor unit test** — the runtime proof must be inside Xphoenix.

## Staged milestones

- **M0 — link feasibility (UNATTENDED-verifiable).** Re-enable glamor in the
  `xorg_server` build (`--enable-glamor`, or compile `libglamor.la` directly) with
  `GLAMOR_HAS_GBM` undefined + the non-empty `glamor_egl_screen_init` shim, and link
  `libGL-phoenix.a`. Success = Xphoenix **statically links** with glamor + our GL and
  zero EGL/GBM/DRM undefined symbols. This empirically proves the decoupling claim.
  (Build-only; no Pi needed.)
- **M1 — runtime accel (HW).** Boot Xphoenix with glamor as the accel backend, root
  pixmap GL-backed, present via `glReadPixels`→`/dev/fb0`. Run an X app; confirm
  GPU-accelerated Render on HDMI, 0 faults. HDMI-verify.
- **M2 — zero-copy present.** Replace the readback blit with
  `st_context_teximage`/scanout page-flip for a true accelerated path.

## Risks / open items for the implementation thrust

- Confirm our Mesa V3D GL actually advertises `ARB_texture_border_clamp`,
  `ARB_fragment_program`, and VAO (very likely — it's Mesa GL 2.1 compat — but
  M0/M1 will surface any gap as a glamor_init failure at a known line).
- glamor↔kdrive pixmap wiring: kdrive's screen pixmap must be created/managed as a
  glamor pixmap; check kdrive's `KdScreenInit`/`fbCreatePixmap` hooks vs glamor's
  `glamor_create_pixmap`.
- This is a **multi-cycle** thrust (build-system + shim + DDX wiring). M0 is the
  next concrete step and is unattended-buildable.

## Bottom line

E5's headline ("GL-windowed apps + HW-accelerated X on V3D") is reachable by wiring
glamor to the GL context we already produce in-process — a bounded shim + build-system
task, not new GPU/DRM engineering. The single-GPU-process constraint is the only
architectural limit, and it does not block an accelerated X desktop (X as sole GPU
owner). Recommended next action: execute **M0** (glamor static-link against
libGL-phoenix.a) as a focused build thrust.
