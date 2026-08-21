# glamor epoxy shim (E5 / G-XORG-MODERN — M0)

glamor (X.Org 2D GL acceleration) hard-depends on **libepoxy** for GL dispatch
(`#include <epoxy/gl.h>` in `glamor_priv.h`, pulled into every core source).
Phoenix has **no libepoxy**, and is static-link only (no dlopen). This shim removes
that dependency the same way the v3d port shims libdrm: since our Mesa GL
(`tools/.gpu-libs/libGL-phoenix.a`) is statically linked and exports the GL symbols
directly, every `glFoo()` binds straight to Mesa, and libepoxy is needed only for a
few version/extension-query helpers.

## Contents

- `epoxy/gl.h` — replaces `<epoxy/gl.h>`: pulls in Mesa's `GL/gl.h`+`GL/glext.h`
  with `GL_GLEXT_PROTOTYPES` (the exact pattern the proven `gl_x11_window.c` GPU
  harness uses) so all GL 2.1 + FBO/VBO/VAO/shader prototypes resolve, and declares
  the 3 `epoxy_*` helpers glamor's core calls.
- `epoxy/egl.h` — empty; glamor includes it only under `GLAMOR_HAS_GBM` (undefined
  on Phoenix), so it is never actually used. Present so any stray include resolves.
- `epoxy_shim.c` — implements `epoxy_gl_version()`, `epoxy_is_desktop_gl()`,
  `epoxy_has_gl_extension()` over `glGetString(GL_VERSION/GL_EXTENSIONS)`. These are
  the ONLY epoxy symbols the glamor core (`libglamor.la`) references;
  `epoxy_has_egl_extension` lives only in the unbuilt `glamor_egl.c`.

Verified: both `epoxy_shim.c` and a glamor-style probe (`#include <epoxy/gl.h>` +
the 3 helpers + `glGenFramebuffers`/`glBindFramebuffer`) cross-compile clean with
`aarch64-phoenix-gcc` against `external/mesa/include` + this dir.

## M0 build integration (next step)

`configure.ac:2051` does `PKG_CHECK_MODULES([GLAMOR], [epoxy])`. There is no real
`epoxy.pc`; instead override the pkg-config result via environment (autoconf skips
the pkg-config query when both `*_CFLAGS` and `*_LIBS` are pre-set), so no `.pc` is
needed:

```sh
SHIM=$PWD/tools/x11-port/glamor-shim
MESA_GL=$PWD/external/mesa/include
GLAMOR_CFLAGS="-I$SHIM -I$MESA_GL" \
GLAMOR_LIBS=" " \
  ./configure --host=aarch64-phoenix --prefix="$PREFIX" \
    --enable-kdrive --enable-glamor \
    --disable-xephyr --disable-xorg ... (rest of the current kdrive flags)
```

Key points:
- `--enable-glamor` (was `--disable-glamor`) makes the build enter `glamor/` and
  build `libglamor.la` + `libglamor_egl_stubs.la`. With `GLAMOR_HAS_GBM` undefined
  (no EGL/GBM/DRM in this env), the core compiles with zero EGL/GBM/DRM symbols.
  The `PKG_CHECK_EXISTS(epoxy >= 1.5.4)` optional-feature probes (configure.ac:2053)
  simply don't fire — they gate EGL-only features we don't build.
- Compile `epoxy_shim.c` into the server link so the 3 helpers resolve.
- Replace the EMPTY upstream `glamor_egl_stubs.c:glamor_egl_screen_init` with a
  non-empty one that installs our phxgl context + `make_current` (the make-or-break
  step from the feasibility doc; model: `glamor_glx_screen_init`).
- Final Xphoenix link adds `tools/.gpu-libs/libGL-phoenix.a` + `libv3d-phoenix.a`.
- **M0 success = Xphoenix statically links with glamor + our GL and zero
  EGL/GBM/DRM undefined symbols.** Remaining GL-entrypoint gaps (if Mesa doesn't
  export a symbol glamor calls) surface here as named undefined symbols.

See `docs/inprogress/2026-08-21-e5-glamor-on-v3d-feasibility.md` for the full plan.
