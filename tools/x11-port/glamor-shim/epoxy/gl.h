/* SPDX-License-Identifier: Zlib
 *
 * Phoenix epoxy shim — <epoxy/gl.h>.
 *
 * glamor uses libepoxy for two things: (a) a GL prototype/dispatch header, and
 * (b) a few epoxy_* version/extension-query helpers. Phoenix has no libepoxy and
 * links Mesa's GL (tools/.gpu-libs/libGL-phoenix.a) statically with no dlopen, so
 * we bind every glFoo() directly to its Mesa symbol — using the exact
 * GL_GLEXT_PROTOTYPES + Mesa GL headers the proven gl_x11_window.c harness uses —
 * and implement only the handful of helpers glamor's core actually calls
 * (epoxy_shim.c). glamor's EGL/GBM/DRI path (glamor_egl.c) is NOT built on Phoenix
 * (GLAMOR_HAS_GBM undefined), so no epoxy EGL surface is needed here.
 *
 * Copyright 2026 Phoenix Systems.
 */
#ifndef PHOENIX_EPOXY_GL_SHIM_H
#define PHOENIX_EPOXY_GL_SHIM_H

#include <stdbool.h>

#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif
#include <GL/gl.h>
#include <GL/glext.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Helpers used by glamor core (glamor.c / glamor_priv.h). Signatures match
 * upstream libepoxy so glamor's call sites compile unchanged. */
int epoxy_gl_version(void);              /* "2.1" -> 21 */
bool epoxy_is_desktop_gl(void);          /* desktop GL (not GLES) */
bool epoxy_has_gl_extension(const char *ext);

#ifdef __cplusplus
}
#endif

#endif /* PHOENIX_EPOXY_GL_SHIM_H */
