/* SPDX-License-Identifier: Zlib
 *
 * glamor_phoenix_ctx.c — E5 / G-XORG-MODERN M1a.
 *
 * Provides the glamor "EGL screen" interface (the four symbols glamor's core
 * leaves as stubs for non-Xorg servers) backed by Phoenix's in-process V3D
 * Gallium/Mesa GL 2.1 context, so the kdrive Xphoenix server can LINK glamor +
 * our static Mesa GL without libEGL/libGBM/libdrm and without libepoxy.
 *
 * This is our OWN Phoenix code, NOT a patch to upstream glamor: it replaces the
 * empty upstream glamor_egl_stubs.c (which is why build-xfbdev.sh --glamor does
 * NOT build glamor/libglamor_egl_stubs.la — that would collide on
 * glamor_egl_screen_init).
 *
 * Header discipline mirrors upstream glamor_glx.c: we deliberately keep the
 * X server / Xlib headers OUT of this translation unit (they clash with Mesa's
 * internal headers on CARD32/Bool/None/Window/...). We include only
 * "glamor_context.h" (the deliberately server-header-free glamor struct) plus
 * the same Mesa internal header set the proven gl_x11_window.c GPU harness uses.
 * Consequently the four glamor interface functions are declared here with
 * `void *` in place of ScreenPtr / PixmapPtr and uint16_t * / uint32_t * in
 * place of CARD16 * / CARD32 *. C linkage matches purely by symbol NAME, so these are
 * link-compatible with glamor's prototypes in glamor.h; we never dereference the
 * opaque server pointers here, so their exact type is irrelevant.
 *
 * The GL bring-up sequence (v3d_phoenix_powerOn -> v3d_screen_create ->
 * context_create -> st_create_context(API_OPENGL_COMPAT) -> _mesa_make_current)
 * and the two link shims (trace_context_create_threaded, pthread_getcpuclockid)
 * are copied verbatim from tools/x11-port/gl_x11_window.c, which is HW-proven.
 *
 * Copyright 2026 Phoenix Systems. Author: Witold Bołt.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>

/* --- Mesa/GL bring-up headers (identical set + order to gl_x11_window.c) ----- */
#include "pipe/p_screen.h"
#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "main/menums.h"
#include "frontend/api.h"
#include "main/mtypes.h"
#include "state_tracker/st_context.h"
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif
#include "GL/gl.h"
#include "GL/glext.h"

/* glamor_context.h references Bool (its trailing glamor_glx_screen_init decl).
 * Provide the same minimal int Bool the epoxy glx shim uses, WITHOUT pulling in
 * any X server / Xlib header (mirrors glamor_glx.c's include discipline). */
typedef int glamor_phx_Bool;
#define Bool glamor_phx_Bool
#include "glamor_context.h"
#undef Bool

/* --- Mesa link shims (verbatim from gl_x11_window.c) -------------------------- */

/* Mesa's trace gallium wrapper is referenced by the GL state tracker but not
 * built into libv3d-phoenix; we never enable GALLIUM_TRACE, so pass the context
 * through (same shim as gl_det_harness.c / gl_x11_window.c). */
struct pipe_context *trace_context_create_threaded(struct pipe_screen *screen,
                                                    struct pipe_context *pipe)
{
	(void)screen;
	return pipe;
}

/* Phoenix libc lacks pthread_getcpuclockid (referenced by Mesa's u_thread
 * timing); monotonic-clock stand-in, same stub as gl_x11_window.c. */
int pthread_getcpuclockid(pthread_t thread, clockid_t *clock_id)
{
	(void)thread;
	if (clock_id)
		*clock_id = CLOCK_MONOTONIC;
	return 0;
}

/* --- Mesa entry points we call (declared here, defined in libGL/libv3d) ------- */
struct pipe_screen_config;
struct renderonly;
struct pipe_screen *v3d_screen_create(int fd, const struct pipe_screen_config *config,
                                      struct renderonly *ro);
extern unsigned char _mesa_make_current(struct gl_context *ctx,
                                        struct gl_framebuffer *drawFb,
                                        struct gl_framebuffer *readFb);
extern int v3d_phoenix_powerOn(void);

/* --- GLX link stubs ----------------------------------------------------------
 * glamor.c:glamor_init() references glamor_glx_screen_init() unconditionally in
 * its non-EGL else-branch (compiled in regardless of the runtime flag), so the
 * linker pulls glamor_glx.o out of libglamor.a, which in turn references these
 * four GLX entrypoints. On Phoenix the current-context path is supplied by our
 * glamor_egl_screen_init below (GLAMOR_USE_EGL_SCREEN), never by GLX, so these
 * only need to LINK — they are never called. Local opaque types (no
 * <epoxy/glx.h> include); the link matches by name. */
typedef struct _XDisplay glamor_phx_Display;
typedef unsigned long glamor_phx_XID;
typedef struct __GLXcontextRec *glamor_phx_GLXContext;

int glXMakeCurrent(glamor_phx_Display *dpy, glamor_phx_XID drawable,
                   glamor_phx_GLXContext ctx)
{
	(void)dpy;
	(void)drawable;
	(void)ctx;
	return 0;
}

glamor_phx_GLXContext glXGetCurrentContext(void)
{
	return NULL;
}

glamor_phx_Display *glXGetCurrentDisplay(void)
{
	return NULL;
}

glamor_phx_XID glXGetCurrentDrawable(void)
{
	return 0;
}

/* --- in-process GL context singleton ----------------------------------------- */

static struct st_context *phx_st = NULL;
static int phx_gl_up = 0;

/* Bring up the V3D Gallium/Mesa GL 2.1 context exactly once. Verbatim sequence
 * from gl_x11_window.c main() (lines 159-172). Returns 0 on success, -1 on any
 * failure. */
static int phx_gl_bringup(void)
{
	struct pipe_screen *pscreen;
	struct pipe_context *pipe;
	static struct gl_config visual;
	static struct st_config_options opts;

	if (phx_gl_up)
		return 0;

	v3d_phoenix_powerOn();

	{
		struct pipe_screen_config cfg;
		memset(&cfg, 0, sizeof(cfg));
		pscreen = v3d_screen_create(0, &cfg, NULL);
	}
	if (pscreen == NULL) {
		fprintf(stderr, "glamor-phx: pipe_screen NULL\n");
		return -1;
	}

	pipe = pscreen->context_create(pscreen, NULL, 0);
	if (pipe == NULL) {
		fprintf(stderr, "glamor-phx: pipe_context NULL\n");
		return -1;
	}

	memset(&visual, 0, sizeof(visual));
	memset(&opts, 0, sizeof(opts));
	phx_st = st_create_context(API_OPENGL_COMPAT, pipe, &visual, NULL, &opts, 0, 0);
	if (phx_st == NULL) {
		fprintf(stderr, "glamor-phx: st_create_context NULL\n");
		return -1;
	}

	_mesa_make_current(phx_st->ctx, NULL, NULL);
	phx_gl_up = 1;
	fprintf(stderr, "glamor-phx: GL up; %s / %s\n",
	        (const char *)glGetString(GL_VERSION),
	        (const char *)glGetString(GL_RENDERER));
	return 0;
}

/* glamor make_current callback: rebind our single global GL context. There is
 * only one Mesa dispatch table, so making our context current is sufficient. */
static void phx_make_current(struct glamor_context *glamor_ctx)
{
	struct st_context *st = (struct st_context *)glamor_ctx->ctx;

	if (st != NULL)
		_mesa_make_current(st->ctx, NULL, NULL);
}

/* --- glamor screen-pixmap readback (E5/M1b present path) ----------------------
 * The kdrive fbdev DDX makes the X screen/root pixmap a glamor GL texture so the
 * root renders on the GPU (fbdevGlamorBackScreenPixmap). To present, it must copy
 * those pixels back into the shadow RAM it write()s to /dev/fb0. This reads `rows`
 * scanlines starting at X y==y0 out of the screen-pixmap texture `tex` (obtained by
 * the DDX from the public glamor_get_pixmap_texture) into `dst`, tightly packed
 * width*4 bytes/row. Uses only public GL: a private color-attachment FBO wrapping
 * the texture + glReadPixels. Touches no glamor internals.
 *
 * Orientation (VERTICAL FLIP REQUIRED — HW-confirmed 2026-08-22): the earlier
 * "no flip needed" reasoning (glamor maps window-top to texel row 0) was WRONG on
 * HW. glReadPixels uses GL's bottom-left origin: reading glReadPixels(0, y0) and
 * writing that to fb/shadow row y0 (top-left origin) places GL row y0 (near the
 * FBO bottom) at the top of the screen -> the whole frame renders UPSIDE DOWN.
 * The owner reported the glamor desktop was flipped, and a programmatic np.flipud
 * of the HDMI grab (artifacts/hdmi/20260821-184717-glamor-desktop-final.png) made
 * every window (xcalc title bar on top, "Phoenix V3D GL" label, all text) read
 * correctly — proving a pure VERTICAL flip. So PHX_READBACK_FLIP_Y is now 1: for
 * an fb band [y0, y0+rows) we read the corresponding GL band [H-(y0+rows), H-y0)
 * and reverse the rows into dst, which is band-correct for both the partial
 * damage-region flush and the full-frame flush (verified: dst row 0 <-> fb row y0).
 *
 * Channel order: the Pi framebuffer really is RGB -- byte0=R, proven on hardware
 * 2026-09-05 with tools/fbprobe, which writes {FF,00,00,00} and {00,00,FF,00} as
 * labelled bands: the first reads RED, the second BLUE. The DDX's
 * redMask=0x000000ff is therefore right, and the SOFTWARE X path renders correct
 * colours (measured: Window Maker's configured rgb:50/50/75 arrives as
 * (79,81,109) on screen).
 *
 * But reading the glamor screen texture with GL_RGBA produced R and B EXCHANGED
 * (the same background came out mauve, ~(117,80,80)): glamor's screen pixmap
 * holds the X pixel bytes in BGRA order on this stack, so GL_RGBA re-labels them
 * and the swap lands on screen. Read GL_BGRA and the bytes arrive in the fb's
 * RGB order unchanged. Do NOT "fix" this in the DDX masks instead -- they are
 * correct, and changing them would break the software path that works.
 */
#define PHX_READBACK_FLIP_Y 1

static GLuint phx_readback_fbo = 0;

void glamor_phx_screen_readback(unsigned int tex, int width, int y0, int rows,
                                void *dst)
{
	GLint prev_fbo = 0;
	static int checked = 0;

	if (phx_st == NULL || tex == 0 || rows <= 0 || dst == NULL)
		return;

	_mesa_make_current(phx_st->ctx, NULL, NULL);

	/* Preserve whatever FBO glamor had bound (cheap insurance; glamor rebinds per
	 * op, but do not assume). */
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);

	if (phx_readback_fbo == 0)
		glGenFramebuffers(1, &phx_readback_fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, phx_readback_fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
	                       (GLuint)tex, 0);

	if (!checked) {
		GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		checked = 1;
		fprintf(stderr, "glamor-phx: screen-readback FBO status 0x%x (%s)\n",
		        status,
		        status == GL_FRAMEBUFFER_COMPLETE ? "complete" : "INCOMPLETE");
	}

	glPixelStorei(GL_PACK_ALIGNMENT, 4);

#if PHX_READBACK_FLIP_Y
	/* Flipped source: the X band [y0, y0+rows) maps to window rows
	 * [H-(y0+rows), H-y0). Read into a scratch band then copy rows reversed so
	 * dst still receives X scanlines top-to-bottom. Only compiled if HW shows a
	 * mirrored frame; the default path above avoids the extra copy. */
	{
		static unsigned char *scratch = NULL;
		static int scratch_bytes = 0;
		int need = width * rows * 4;
		int r;

		if (need > scratch_bytes) {
			free(scratch);
			scratch = (unsigned char *)malloc(need);
			scratch_bytes = scratch ? need : 0;
		}
		if (scratch) {
			int H = 0;
			/* The X band [y0, y0+rows) maps to the mirrored window origin
			 * H-(y0+rows). Bind the screen-pixmap texture first so the height
			 * query reads THIS texture (it is only attached to the FBO above,
			 * not bound to the active unit). */
			glBindTexture(GL_TEXTURE_2D, (GLuint)tex);
			glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &H);
			glReadPixels(0, H - (y0 + rows), width, rows, GL_BGRA,
			             GL_UNSIGNED_BYTE, scratch);
			for (r = 0; r < rows; r++)
				memcpy((unsigned char *)dst + (size_t)r * width * 4,
				       scratch + (size_t)(rows - 1 - r) * width * 4,
				       (size_t)width * 4);
		}
	}
#else
	glReadPixels(0, y0, width, rows, GL_BGRA, GL_UNSIGNED_BYTE, dst);
#endif

	glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
}

/* --- glamor EGL-screen interface (replaces glamor/glamor_egl_stubs.c) ---------
 * Signatures are link-compatible with glamor.h by symbol name; `void *` stands
 * in for ScreenPtr / PixmapPtr and uint16_t * / uint32_t * for CARD16 * / CARD32 * so no
 * server header is needed (see the file banner). */

void glamor_egl_screen_init(void *screen, struct glamor_context *glamor_ctx)
{
	(void)screen;

	if (phx_gl_bringup() != 0) {
		fprintf(stderr, "glamor-phx: GL bring-up FAILED; glamor accel unavailable\n");
		/* Leave make_current NULL-safe: glamor_make_current() guards on the
		 * callback, and a subsequent glGetString in glamor_init will fail the
		 * server's glamor path gracefully rather than crash here. */
		glamor_ctx->ctx = NULL;
		glamor_ctx->display = NULL;
		glamor_ctx->make_current = phx_make_current;
		return;
	}

	glamor_ctx->ctx = (void *)phx_st;
	glamor_ctx->display = (void *)phx_st; /* non-NULL: glamor treats it opaquely */
	glamor_ctx->drawable_xid = 0;
	glamor_ctx->make_current = phx_make_current;
}

/* fd-exporter stubs: DRI3/GBM buffer export is not supported on Phoenix
 * (GLAMOR_HAS_GBM undefined, DRI3 disabled). Same return values as the empty
 * upstream glamor_egl_stubs.c. Provided here so the core links without building
 * libglamor_egl_stubs.la (which would duplicate glamor_egl_screen_init). */

int glamor_egl_fd_name_from_pixmap(void *screen, void *pixmap,
                                   uint16_t *stride, uint32_t *size)
{
	(void)screen;
	(void)pixmap;
	(void)stride;
	(void)size;
	return -1;
}

int glamor_egl_fd_from_pixmap(void *screen, void *pixmap,
                              uint16_t *stride, uint32_t *size)
{
	(void)screen;
	(void)pixmap;
	(void)stride;
	(void)size;
	return -1;
}

int glamor_egl_fds_from_pixmap(void *screen, void *pixmap, int *fds,
                               uint32_t *offsets, uint32_t *strides,
                               uint64_t *modifier)
{
	(void)screen;
	(void)pixmap;
	(void)fds;
	(void)offsets;
	(void)strides;
	(void)modifier;
	return 0;
}
