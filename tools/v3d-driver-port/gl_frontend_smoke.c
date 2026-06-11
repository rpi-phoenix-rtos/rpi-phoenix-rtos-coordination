/*
 * gl_frontend_smoke.c — GLQuake Path-C Phase-4 GL frontend correctness:
 * wrap the v3d pipe_context in a Mesa GL context, make it current (surfaceless),
 * wrap an RT pipe_resource as a GL texture via st_context_teximage, attach it to
 * an FBO, and glClear it to green — the GL equivalent of the proven v3d render-
 * clear, but driven entirely through the OpenGL API. Then read the RT back.
 *
 * Surfaceless + FBO avoids the winsys-framebuffer/drawable machinery: the GL
 * context has no default framebuffer (_mesa_make_current(ctx,NULL,NULL)); we
 * render to a user FBO backed by our own RT (st_context_teximage).
 *
 * Copyright 2026 Phoenix Systems  %LICENSE%
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "pipe/p_screen.h"
#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "util/box.h"
#include "main/menums.h"                 /* gl_api */
#include "frontend/api.h"                /* st_config_options */
#include "main/mtypes.h"                 /* gl_config, gl_context */
#include "state_tracker/st_context.h"    /* st_create_context, st_context_teximage */
#define GL_GLEXT_PROTOTYPES 1
#include "GL/gl.h"
#include "GL/glext.h"

struct pipe_screen_config;
struct renderonly;
struct pipe_screen *v3d_screen_create(int fd, const struct pipe_screen_config *config, struct renderonly *ro);
/* mesa internal make-current (main/context.h) */
extern unsigned char _mesa_make_current(struct gl_context *ctx,
                                        struct gl_framebuffer *drawFb,
                                        struct gl_framebuffer *readFb);

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	struct pipe_screen_config cfg;
	memset(&cfg, 0, sizeof(cfg));
	struct pipe_screen *pscreen = v3d_screen_create(0, &cfg, NULL);
	if (!pscreen) { printf("gl: pipe_screen NULL\n"); return 1; }
	struct pipe_context *pipe = pscreen->context_create(pscreen, NULL, 0);
	if (!pipe) { printf("gl: pipe_context NULL\n"); return 1; }

	struct gl_config visual;
	struct st_config_options opts;
	memset(&visual, 0, sizeof(visual));
	memset(&opts, 0, sizeof(opts));
	struct st_context *st = st_create_context(API_OPENGL_COMPAT, pipe, &visual,
	                                          NULL, &opts, 0, 0);
	if (!st) { printf("gl: st_create_context NULL\n"); return 1; }
	printf("gl: context created\n");

	/* surfaceless make-current: no default framebuffer, FBO-only. */
	_mesa_make_current(st->ctx, NULL, NULL);
	printf("gl: made current; GL_VERSION=%s\n", (const char *)glGetString(GL_VERSION));

	/* our render target, wrapped as a GL texture */
	struct pipe_resource rtt = { 0 };
	rtt.target = PIPE_TEXTURE_2D; rtt.format = PIPE_FORMAT_R8G8B8A8_UNORM;
	rtt.width0 = 256; rtt.height0 = 256; rtt.depth0 = 1; rtt.array_size = 1;
	rtt.bind = PIPE_BIND_RENDER_TARGET | PIPE_BIND_SAMPLER_VIEW;
	struct pipe_resource *rt = pscreen->resource_create(pscreen, &rtt);
	if (!rt) { printf("gl: RT NULL\n"); return 1; }

	GLuint tex = 0, fbo = 0;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	st_context_teximage(st, GL_TEXTURE_2D, 0, PIPE_FORMAT_R8G8B8A8_UNORM, rt, 0);
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
	GLenum fbs = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	printf("gl: FBO status=0x%x (complete=0x%x)\n", fbs, GL_FRAMEBUFFER_COMPLETE);

	glViewport(0, 0, 256, 256);
	glClearColor(0.0f, 1.0f, 0.0f, 1.0f);  /* green */
	glClear(GL_COLOR_BUFFER_BIT);
	glFinish();
	printf("gl: glClear+glFinish done\n");

	/* read the RT back */
	struct pipe_box box = { 0 };
	box.width = 256; box.height = 256; box.depth = 1;
	struct pipe_transfer *xfer = NULL;
	void *map = pipe->texture_map(pipe, rt, 0, PIPE_MAP_READ, &box, &xfer);
	if (map) {
		uint32_t px = ((volatile uint32_t *)map)[128 * 256 + 128];
		printf("gl: GLCLEAR readback center=0x%08x (expect green 0xff00ff00)\n", px);
		pipe->texture_unmap(pipe, xfer);
	}
	printf("gl: GLCLEAR-DONE (Mesa OpenGL clear on the V3D)\n");
	return 0;
}
