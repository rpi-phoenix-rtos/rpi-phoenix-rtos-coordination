/*
 * pl_phoenix_glctx.c — V3D/Mesa GL context + offscreen FBO for the Quakespasm
 * Phoenix port. Mesa-header-only (pipe/st), deliberately kept separate from Quake's
 * headers (pl_phoenix_vid.c) to avoid type collisions. Same recipe as rpi4-glcube:
 * surfaceless st_create_context + a renderbuffer-backed RGBA8 + DEPTH24 FBO that
 * Quake renders into; pl_phoenix_vid.c presents it to /dev/fb0.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

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

struct pipe_screen_config;
struct renderonly;
struct pipe_screen *v3d_screen_create(int fd, const struct pipe_screen_config *config,
                                      struct renderonly *ro);
extern unsigned char _mesa_make_current(struct gl_context *ctx,
                                        struct gl_framebuffer *drawFb,
                                        struct gl_framebuffer *readFb);

/* winsys (v3d_phoenix_winsys.c): one-shot request that the NEXT BO be backed by the scanout fb,
 * and a query of whether the single scanout surface has been claimed. */
extern void v3d_phoenix_set_next_scanout(void);
extern int  v3d_phoenix_scanout_active(void);

static struct st_context *g_st = NULL;

/* RENDER-TO-SCANOUT was the GPU storing the depth-tested frame straight into the LIVE HDMI fb,
 * which the HVS display controller reads continuously — that GPU-write/display-read contention
 * stalls the V3D depth-output FIFO on heavy frames (the intermittent render wedge, confirmed:
 * the same geometry to a DRAM RT never wedges). Decouple it: Quake renders into a DRAM color RT
 * (+ depth), then a per-frame COLOR-ONLY GPU blit resolves that to the scanout fb. The blit has
 * no depth FIFO and is a single light streaming pass, so it cannot hit the stall. */
static GLuint g_render_fbo  = 0;     /* DRAM color+depth FBO — Quake renders here (no fb contention) */
static GLuint g_scanout_fbo = 0;     /* scanout-fb-backed color FBO — blit-resolve destination */
static int    g_resolve     = 0;     /* 1 = 2-FBO blit-resolve path active (scanout fb claimed) */
static int    g_w = 0, g_h = 0;

/* Bind the DRAM render FBO. Quake renders to the "default" framebuffer (0), incomplete on a
 * surfaceless context; redirect each frame into our readable render FBO. */
void qsv3d_bind_fbo(void)
{
	if (g_render_fbo != 0)
		glBindFramebuffer(GL_FRAMEBUFFER, g_render_fbo);
}

/* Resolve the just-rendered DRAM frame to the scanout fb (color-only GPU blit). Returns 1 if it
 * resolved (2-FBO path), 0 if not active (single-FBO + CPU-present fallback). Both FBOs are
 * full-screen so Mesa forced Y_0_TOP on both (st_atom_framebuffer.c); a (0..h)->(0..h) blit
 * between matching-orientation framebuffers copies verbatim, landing upright on the y-down fb. */
int qsv3d_resolve(void)
{
	if (!g_resolve)
		return 0;
	glBindFramebuffer(GL_READ_FRAMEBUFFER, g_render_fbo);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_scanout_fbo);
	glBlitFramebuffer(0, 0, g_w, g_h, 0, 0, g_w, g_h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	glBindFramebuffer(GL_FRAMEBUFFER, g_render_fbo);   /* restore for the next frame */
	glFinish();
	return 1;
}

int qsv3d_init(int w, int h)
{
	struct pipe_screen_config cfg;
	struct pipe_screen *pscreen;
	struct pipe_context *pipe;
	struct gl_config visual;
	struct st_config_options opts;
	struct st_context *st;
	GLuint fbo = 0, rbColor = 0, rbDepth = 0;
	GLenum fbs;

	memset(&cfg, 0, sizeof(cfg));
	pscreen = v3d_screen_create(0, &cfg, NULL);
	if (!pscreen) { printf("qsv3d: pipe_screen NULL\n"); return 1; }
	pipe = pscreen->context_create(pscreen, NULL, 0);
	if (!pipe) { printf("qsv3d: pipe_context NULL\n"); return 1; }

	memset(&visual, 0, sizeof(visual));
	memset(&opts, 0, sizeof(opts));
	st = st_create_context(API_OPENGL_COMPAT, pipe, &visual, NULL, &opts, 0, 0);
	if (!st) { printf("qsv3d: st_create_context NULL\n"); return 1; }
	_mesa_make_current(st->ctx, NULL, NULL);
	g_st = st;

	printf("qsv3d: GL up; %s / %s\n",
	       (const char *)glGetString(GL_VERSION),
	       (const char *)glGetString(GL_RENDERER));

	g_w = w; g_h = h;

	/* SCANOUT destination FBO FIRST: set_next_scanout() makes its color renderbuffer claim the
	 * single scanout surface (the HDMI fb). Created before the render FBO so the render FBO's
	 * color falls to plain DRAM. No depth attachment — it is only a blit target. */
	{
		GLuint rbScan = 0;
		glGenFramebuffers(1, &g_scanout_fbo);
		glBindFramebuffer(GL_FRAMEBUFFER, g_scanout_fbo);
		glGenRenderbuffers(1, &rbScan);
		glBindRenderbuffer(GL_RENDERBUFFER, rbScan);
		v3d_phoenix_set_next_scanout();   /* the next RT alloc claims the scanout fb */
		glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, w, h);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rbScan);
		g_resolve = v3d_phoenix_scanout_active();   /* did the fb get claimed? */
		printf("qsv3d: scanout FBO %dx%d resolve=%d (scanout %s)\n",
		       w, h, g_resolve, g_resolve ? "claimed" : "unavailable -> CPU-present fallback");
	}

	/* RENDER FBO: DRAM color + depth. Quake renders depth-tested geometry here, off the live fb
	 * (no display contention -> no depth-output stall). With scanout already claimed above, this
	 * color renderbuffer is backed by plain DRAM. */
	glGenFramebuffers(1, &fbo);
	g_render_fbo = fbo;
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glGenRenderbuffers(1, &rbColor);
	glBindRenderbuffer(GL_RENDERBUFFER, rbColor);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, w, h);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rbColor);
	glGenRenderbuffers(1, &rbDepth);
	glBindRenderbuffer(GL_RENDERBUFFER, rbDepth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rbDepth);
	fbs = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	printf("qsv3d: render FBO %dx%d status=0x%x (complete=0x%x)\n",
	       w, h, fbs, GL_FRAMEBUFFER_COMPLETE);

	glViewport(0, 0, w, h);

	/* Initialize both FBOs to black so an un-rendered frame shows black, not garbage. */
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	if (g_resolve) {
		glBindFramebuffer(GL_FRAMEBUFFER, g_scanout_fbo);
		glClear(GL_COLOR_BUFFER_BIT);
		glBindFramebuffer(GL_FRAMEBUFFER, g_render_fbo);
	}
	glFinish();
	return 0;
}

void qsv3d_make_current(void)
{
	if (g_st)
		_mesa_make_current(g_st->ctx, NULL, NULL);
}

/* Mesa's trace gallium wrapper (referenced by the GL state tracker) is not built
 * into libv3d-phoenix; we never enable GALLIUM_TRACE, so pass the context through. */
struct pipe_context *trace_context_create_threaded(struct pipe_screen *screen,
                                                    struct pipe_context *pipe)
{
	(void)screen;
	return pipe;
}
