/* SPDX-License-Identifier: Zlib
 *
 * Phoenix epoxy shim — <epoxy/glx.h>.
 *
 * glamor's core builds glamor_glx.c (it is in libglamor_la_SOURCES), the
 * "adopt an already-current GLX context" path. That file includes
 * <epoxy/glx.h> for the GLX entrypoints and the Xlib GLX types. Phoenix has
 * no libepoxy and no Xlib GLX, but glamor_glx.c only ever *references* five
 * GLX symbols and a handful of types — and libglamor.la is a static archive,
 * so the symbols need only be *declared*, not defined, for it to build. (Whether
 * these GLX entrypoints resolve at the final Xphoenix link is an M1 question;
 * on Phoenix the make_current path is supplied by the non-empty
 * glamor_egl_screen_init shim, not GLX.)
 *
 * This header therefore provides self-contained minimal declarations rather
 * than pulling in real GL/glx.h + Xlib (which Phoenix's X11 headers make heavy
 * and which the "conflicting CARD32 typedefs" comment in glamor_glx.c warns
 * against). The types are chosen to match how glamor_context.h stores them
 * (display/ctx as void *, drawable_xid as uint32_t).
 *
 * Copyright 2026 Phoenix Systems.
 */
#ifndef PHOENIX_EPOXY_GLX_SHIM_H
#define PHOENIX_EPOXY_GLX_SHIM_H

#include <stdint.h>

/* GL types (glamor_glx.c does not use GL calls itself, but keep the GL surface
 * consistent with the rest of the shim in case of transitive includes). */
#include <epoxy/gl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Xlib primitives glamor_glx.c / glamor_context.h reference. Real libepoxy
 * pulls these from Xlib; here we declare the minimal set. */
#ifndef Bool
typedef int Bool;
#endif
#ifndef True
#define True  1
#endif
#ifndef False
#define False 0
#endif
#ifndef None
#define None  0L
#endif

/* Opaque Xlib Display and GLX handle types. glamor stores display/ctx as
 * void * and drawable_xid as uint32_t, so exact layout is irrelevant — only
 * that these are pointer / integer typed for the glX* prototypes below. */
typedef struct _XDisplay Display;
typedef unsigned long XID;
typedef XID GLXDrawable;
typedef struct __GLXcontextRec *GLXContext;

/* The five GLX entrypoints glamor_glx.c references. Declarations only. */
extern Bool glXMakeCurrent(Display *dpy, GLXDrawable drawable, GLXContext ctx);
extern GLXContext glXGetCurrentContext(void);
extern Display *glXGetCurrentDisplay(void);
extern GLXDrawable glXGetCurrentDrawable(void);

#ifdef __cplusplus
}
#endif

#endif /* PHOENIX_EPOXY_GLX_SHIM_H */
