/* SPDX-License-Identifier: Zlib
 *
 * gl_x11_window.c — D1/D2 "accelerated GL in an X window" harness for Phoenix-RTOS
 * (Raspberry Pi 4, V3D 4.2).
 *
 * Combines two already-proven pieces into one static aarch64-phoenix ELF:
 *   (a) the offscreen GL render+readback path from tools/v3d-driver-port/gl_det_harness.c
 *       (v3d_screen_create -> st_create_context(API_OPENGL_COMPAT) -> a DRAM RGBA8 +
 *       DEPTH24 FBO, render, glFinish, glReadPixels); and
 *   (b) an ordinary libX11 client (like the xeyes/twm clients that already run on
 *       Phoenix), which opens the Xphoenix display, creates a window, and presents
 *       CPU-side pixels with XPutImage.
 *
 * Each frame: an animated depth-tested scene is rendered by the V3D GPU into the
 * offscreen FBO, read back with glReadPixels, packed into an XImage honouring the
 * window visual's channel masks (with a vertical flip, since glReadPixels' origin is
 * bottom-left while X's is top-left), and blitted into the window with XPutImage.
 * The result is GPU-rendered pixels in a normal X window under the Xphoenix server.
 *
 * This is our own code — NOT derived from any GPL source. Zlib licensed.
 *
 * Copyright 2026 Phoenix Systems. Author: Witold Bołt.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <math.h>

/* --- Mesa/GL bring-up headers (identical set to gl_det_harness.c) ------------- */
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

/* --- X11 client headers LAST, so Mesa's internal headers cannot shadow Xlib's
 *     tokens (Status/None/Bool/Window/...). If a future Mesa header ever leaks one
 *     of those as a macro, #undef it right here before the X includes. ---------- */
#include <X11/Xlib.h>
#include <X11/Xutil.h>

/* Same extern decls + stubs as gl_det_harness.c (the offscreen GL path). */
struct pipe_screen_config;
struct renderonly;
struct pipe_screen *v3d_screen_create(int fd, const struct pipe_screen_config *config,
                                      struct renderonly *ro);
extern unsigned char _mesa_make_current(struct gl_context *ctx,
                                        struct gl_framebuffer *drawFb,
                                        struct gl_framebuffer *readFb);
extern int v3d_phoenix_powerOn(void);

/* Mesa's trace gallium wrapper is referenced by the GL state tracker but not built
 * into libv3d-phoenix; we never enable GALLIUM_TRACE, so pass the context through
 * (same shim as gl_det_harness.c / pl_phoenix_glctx.c). */
struct pipe_context *trace_context_create_threaded(struct pipe_screen *screen,
                                                    struct pipe_context *pipe)
{
	(void)screen;
	return pipe;
}

/* Phoenix libc lacks pthread_getcpuclockid (referenced by Mesa's u_thread timing);
 * monotonic-clock stand-in, same stub as gl_det_harness.c / pl_phoenix_stubs.c. */
int pthread_getcpuclockid(pthread_t thread, clockid_t *clock_id)
{
	(void)thread;
	if (clock_id)
		*clock_id = CLOCK_MONOTONIC;
	return 0;
}

#define W 640
#define H 480
#define NFRAMES 20000   /* long enough (~5+ min of animation) that the periodic HDMI
                         * snapshots (every ~25 s) reliably catch the window on-screen;
                         * the test cycle powers off at the end regardless. */

/* Trailing-zero count of a channel mask -> the shift needed to place an 8-bit
 * channel value into that mask's low bit. (Masks are assumed byte-aligned 8-bit
 * channels, the TrueColor case; any width is handled by shifting the top 8 bits.) */
static int mask_shift(unsigned long mask)
{
	if (mask == 0)
		return 0;
	return __builtin_ctzl(mask);
}

/* Number of set bits in a channel mask (channel width, typ. 8). */
static int mask_width(unsigned long mask)
{
	int w = 0;
	while (mask) { w += (int)(mask & 1ul); mask >>= 1; }
	return w;
}

/* Rescale an 8-bit channel value to a mask of width `wd`. Handles wd<8 (the
 * fbdev's 8-bit TrueColor channels: shift down), wd==8 (identity) and wd>8
 * (a >8bpc visual: shift up) without ever shifting by a negative count. */
static unsigned long scale8_to_width(unsigned v, int wd)
{
	if (wd >= 8)
		return (unsigned long)v << (wd - 8);
	return (unsigned long)v >> (8 - wd);
}

/* An animated, depth-tested scene into the currently-bound FBO. A rotating pinwheel
 * of coloured triangles placed at varied depths, so LEQUAL depth-test accepts/rejects
 * across the fan (exercises the same EZ/tile path the GL frontend drives). `angle` is
 * advanced per frame so the motion is visible in the presented window. */
__attribute__((unused)) static void draw_scene(float angle)
{
	const int spokes = 12;

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glRotatef(angle, 0.0f, 0.0f, 1.0f);
	glRotatef(angle * 0.6f, 1.0f, 0.0f, 0.0f);

	glBegin(GL_TRIANGLES);
	for (int s = 0; s < spokes; s++) {
		float a0 = (float)s / (float)spokes * 6.2831853f;
		float a1 = (float)(s + 1) / (float)spokes * 6.2831853f;
		/* alternate depth per spoke so the depth test culls overlaps differently
		 * as the fan spins */
		float z = (s & 1) ? -0.4f : 0.4f;
		float r = (float)s / (float)spokes;
		float g = 1.0f - r;
		float b = 0.5f + 0.5f * (float)((s >> 1) & 1);

		glColor3f(r, g, b);
		glVertex3f(0.0f, 0.0f, 0.0f);
		glColor3f(g, b, r);
		glVertex3f(0.85f * cosf(a0), 0.85f * sinf(a0), z);
		glColor3f(b, r, g);
		glVertex3f(0.85f * cosf(a1), 0.85f * sinf(a1), z);
	}
	glEnd();
}

/* DIAGNOSTIC (2026-08-26, owner-requested): an UNAMBIGUOUSLY ORIENTED flat scene to
 * tell apart flip vs. offset vs. rotation of the presented content. The rotating
 * pinwheel above is too symmetric to read orientation from an HDMI grab. Drawn in
 * NDC with identity matrices, depth test off:
 *   - three horizontal colour bands: RED top (y>+0.33), GREEN middle, BLUE bottom
 *     (y<-0.33)  -> distinguishes top/bottom inversion;
 *   - a WHITE square in the TOP-LEFT NDC corner -> flip vs. rotation;
 *   - a YELLOW up-pointing triangle (apex at top centre) -> confirms "up".
 * The client's glReadPixels+vertical-flip makes GL-top land at X-image row 0, so a
 * correct present shows: red band at window top, blue at bottom, white marker
 * top-left, arrow apex up. Any deviation names the exact transform. */
static void draw_orient_scene(void)
{
	glDisable(GL_DEPTH_TEST);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glClearColor(0.10f, 0.10f, 0.10f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glBegin(GL_QUADS);
	/* RED top band */
	glColor3f(0.9f, 0.1f, 0.1f);
	glVertex2f(-1.0f, 1.0f);   glVertex2f(1.0f, 1.0f);
	glVertex2f(1.0f, 0.333f);  glVertex2f(-1.0f, 0.333f);
	/* GREEN middle band */
	glColor3f(0.1f, 0.8f, 0.1f);
	glVertex2f(-1.0f, 0.333f);  glVertex2f(1.0f, 0.333f);
	glVertex2f(1.0f, -0.333f);  glVertex2f(-1.0f, -0.333f);
	/* BLUE bottom band */
	glColor3f(0.1f, 0.2f, 0.9f);
	glVertex2f(-1.0f, -0.333f); glVertex2f(1.0f, -0.333f);
	glVertex2f(1.0f, -1.0f);    glVertex2f(-1.0f, -1.0f);
	/* WHITE marker square, TOP-LEFT NDC corner */
	glColor3f(1.0f, 1.0f, 1.0f);
	glVertex2f(-0.95f, 0.95f);  glVertex2f(-0.60f, 0.95f);
	glVertex2f(-0.60f, 0.60f);  glVertex2f(-0.95f, 0.60f);
	glEnd();

	/* YELLOW up-pointing triangle (apex at top centre) */
	glBegin(GL_TRIANGLES);
	glColor3f(1.0f, 1.0f, 0.0f);
	glVertex2f(0.0f, 0.85f);    /* apex, top */
	glVertex2f(-0.35f, -0.55f); /* base left */
	glVertex2f(0.35f, -0.55f);  /* base right */
	glEnd();
}

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);
	fprintf(stderr, "gl-x11: start (%dx%d, %d frames)\n", W, H, NFRAMES);

	/* ---- 1. GL bring-up (verbatim from gl_det_harness.c) -------------------- */
	v3d_phoenix_powerOn();

	struct pipe_screen_config cfg;
	memset(&cfg, 0, sizeof(cfg));
	struct pipe_screen *pscreen = v3d_screen_create(0, &cfg, NULL);
	if (!pscreen) { fprintf(stderr, "gl-x11: pipe_screen NULL\n"); return 1; }
	struct pipe_context *pipe = pscreen->context_create(pscreen, NULL, 0);
	if (!pipe) { fprintf(stderr, "gl-x11: pipe_context NULL\n"); return 1; }
	struct gl_config visual; struct st_config_options opts;
	memset(&visual, 0, sizeof(visual)); memset(&opts, 0, sizeof(opts));
	struct st_context *st = st_create_context(API_OPENGL_COMPAT, pipe, &visual, NULL, &opts, 0, 0);
	if (!st) { fprintf(stderr, "gl-x11: st_create_context NULL\n"); return 1; }
	_mesa_make_current(st->ctx, NULL, NULL);
	fprintf(stderr, "gl-x11: GL up; %s / %s\n", (const char *)glGetString(GL_VERSION),
	        (const char *)glGetString(GL_RENDERER));

	/* DRAM color+depth FBO (mirrors gl_det_harness.c) — glReadPixels reads its
	 * CPU-mapped BO directly (no scanout/fb0). */
	GLuint fbo = 0, rbColor = 0, rbDepth = 0;
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glGenRenderbuffers(1, &rbColor);
	glBindRenderbuffer(GL_RENDERBUFFER, rbColor);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, W, H);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rbColor);
	glGenRenderbuffers(1, &rbDepth);
	glBindRenderbuffer(GL_RENDERBUFFER, rbDepth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, W, H);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rbDepth);
	GLenum fbs = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	fprintf(stderr, "gl-x11: FBO %dx%d status=0x%x (complete=0x%x)\n", W, H, fbs, GL_FRAMEBUFFER_COMPLETE);
	if (fbs != GL_FRAMEBUFFER_COMPLETE) { fprintf(stderr, "gl-x11: FBO incomplete ABORT\n"); return 2; }

	glViewport(0, 0, W, H);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glClearColor(0.05f, 0.06f, 0.12f, 1.0f);

	/* ---- 2. X11 window ------------------------------------------------------ */
	Display *dpy = XOpenDisplay(NULL);
	if (!dpy)
		dpy = XOpenDisplay(":0");
	if (!dpy) { fprintf(stderr, "gl-x11: XOpenDisplay failed (DISPLAY=%s)\n",
	                    getenv("DISPLAY") ? getenv("DISPLAY") : "(unset)"); return 3; }

	int screen = DefaultScreen(dpy);
	Window root = RootWindow(dpy, screen);
	/* Place at a fixed offset (not 0,0) so it's clearly a windowed, WM-decorated
	 * client rather than a fullscreen root paint. */
	int wx = 300, wy = 180;
	Window win = XCreateSimpleWindow(dpy, root, wx, wy, W, H, 0,
	                                 BlackPixel(dpy, screen), BlackPixel(dpy, screen));
	XStoreName(dpy, win, "Phoenix V3D GL");
	/* WM size hints with USPosition|USSize: a window manager (twm) honours the
	 * user-specified position and decorates + places the window IMMEDIATELY, instead
	 * of triggering twm's interactive rubber-band placement (which would need a mouse
	 * click and is useless for an automated HDMI grab). Harmless with no WM running. */
	XSizeHints *hints = XAllocSizeHints();
	if (hints != NULL) {
		hints->flags = USPosition | USSize | PPosition | PSize | PMinSize | PMaxSize;
		hints->x = wx; hints->y = wy;
		hints->width = W; hints->height = H;
		hints->min_width = hints->max_width = W;   /* fixed-size (no resize) — the FBO is W x H */
		hints->min_height = hints->max_height = H;
		XSetWMNormalHints(dpy, win, hints);
		XFree(hints);
	}
	XSelectInput(dpy, win, ExposureMask);
	XMapWindow(dpy, win);
	GC gc = XCreateGC(dpy, win, 0, NULL);

	/* Query the window's actual visual + depth so the XImage packing matches the
	 * server's pixel format. */
	XWindowAttributes wa;
	XGetWindowAttributes(dpy, win, &wa);
	Visual *vis = wa.visual;
	int depth = wa.depth;
	unsigned long rmask = vis->red_mask, gmask = vis->green_mask, bmask = vis->blue_mask;
	int rsh = mask_shift(rmask), gsh = mask_shift(gmask), bsh = mask_shift(bmask);
	int rwd = mask_width(rmask), gwd = mask_width(gmask), bwd = mask_width(bmask);
	fprintf(stderr, "gl-x11: window visual depth=%d masks r=0x%lx g=0x%lx b=0x%lx byte_order=%s\n",
	        depth, rmask, gmask, bmask, ImageByteOrder(dpy) == LSBFirst ? "LSBFirst" : "MSBFirst");

	/* 32-bpp client-side image buffer (w*h*4). */
	uint32_t *ximg_data = (uint32_t *)malloc((size_t)W * H * 4);
	if (!ximg_data) { fprintf(stderr, "gl-x11: OOM ximg\n"); return 4; }
	XImage *img = XCreateImage(dpy, vis, depth, ZPixmap, 0,
	                           (char *)ximg_data, W, H, 32, 0);
	if (!img) { fprintf(stderr, "gl-x11: XCreateImage failed\n"); return 4; }
	/* We fill the buffer as native-endian 32-bit words below; declare the image's
	 * byte order to match the host so XPutImage does not byte-swap. Xphoenix and the
	 * aarch64 client are both little-endian. */
	img->byte_order = LSBFirst;

	/* readback scratch: RGBA8, one full frame. */
	unsigned char *rgba = (unsigned char *)malloc((size_t)W * H * 4);
	if (!rgba) { fprintf(stderr, "gl-x11: OOM rgba\n"); return 4; }

	/* ---- 3. render / readback / present loop -------------------------------- */
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	for (int frame = 0; frame < NFRAMES; frame++) {
		float angle = (float)frame * 2.0f;

		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		(void)angle;             /* DIAGNOSTIC: oriented static scene (owner 2026-08-26) */
		draw_orient_scene();     /* was: draw_scene(angle) — restore after Y-flip diagnosis */
		glFinish();
		glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

		/* pack RGBA readback into the XImage, honouring the visual masks and
		 * flipping vertically (glReadPixels origin bottom-left -> X top-left). */
		for (int y = 0; y < H; y++) {
			const unsigned char *srow = rgba + (size_t)(H - 1 - y) * W * 4;
			uint32_t *drow = ximg_data + (size_t)y * W;
			for (int x = 0; x < W; x++) {
				unsigned r = srow[x * 4 + 0];
				unsigned g = srow[x * 4 + 1];
				unsigned b = srow[x * 4 + 2];
				/* scale 8-bit channel to the mask's width, then shift into place */
				unsigned long pr = scale8_to_width(r, rwd) << rsh;
				unsigned long pg = scale8_to_width(g, gwd) << gsh;
				unsigned long pb = scale8_to_width(b, bwd) << bsh;
				drow[x] = (uint32_t)((pr & rmask) | (pg & gmask) | (pb & bmask));
			}
		}

		XPutImage(dpy, win, gc, img, 0, 0, 0, 0, W, H);
		XFlush(dpy);

		/* drain pending events; re-present on Expose, ignore the rest. */
		while (XPending(dpy)) {
			XEvent ev;
			XNextEvent(dpy, &ev);
			if (ev.type == Expose) {
				XPutImage(dpy, win, gc, img, 0, 0, 0, 0, W, H);
				XFlush(dpy);
			}
		}

		if ((frame % 30) == 0)
			fprintf(stderr, "gl-x11: frame %d/%d angle=%.0f\n", frame, NFRAMES, angle);

		usleep(16000);
	}

	fprintf(stderr, "gl-x11: DONE (%d frames presented)\n", NFRAMES);

	/* XDestroyImage frees ximg_data too; free rgba separately. */
	XDestroyImage(img);
	free(rgba);
	XFreeGC(dpy, gc);
	XDestroyWindow(dpy, win);
	XCloseDisplay(dpy);
	return 0;
}
