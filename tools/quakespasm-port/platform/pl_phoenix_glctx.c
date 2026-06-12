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

static struct st_context *g_st = NULL;

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

	glGenFramebuffers(1, &fbo);
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
	printf("qsv3d: FBO %dx%d status=0x%x (complete=0x%x)\n",
	       w, h, fbs, GL_FRAMEBUFFER_COMPLETE);

	glViewport(0, 0, w, h);
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
