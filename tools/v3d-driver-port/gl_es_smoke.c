/*
 * gl_es_smoke.c — OpenGL **ES** frontend bring-up: the GLES sibling of
 * gl_frontend_smoke.c. Wrap the v3d pipe_context in a Mesa GL context created
 * with API_OPENGLES2 (instead of API_OPENGL_COMPAT), make it current surfaceless,
 * wrap an RT as a GL texture, attach to an FBO, glClear to green, and read it
 * back. Proves the gallium v3d driver + st/mesa serve an OpenGL ES context on the
 * real V3D 4.2 — the enabler for the GLES game renderers (yQuake2 gl3, later STK).
 * The st/mesa + v3d path is identical to desktop GL; only the gl_api enum differs.
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
extern unsigned char _mesa_make_current(struct gl_context *ctx,
                                        struct gl_framebuffer *drawFb,
                                        struct gl_framebuffer *readFb);

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	struct pipe_screen_config cfg;
	memset(&cfg, 0, sizeof(cfg));
	struct pipe_screen *pscreen = v3d_screen_create(0, &cfg, NULL);
	if (!pscreen) { printf("gles: pipe_screen NULL\n"); return 1; }
	struct pipe_context *pipe = pscreen->context_create(pscreen, NULL, 0);
	if (!pipe) { printf("gles: pipe_context NULL\n"); return 1; }

	struct gl_config visual;
	struct st_config_options opts;
	memset(&visual, 0, sizeof(visual));
	memset(&opts, 0, sizeof(opts));
	/* THE ONE DIFFERENCE vs desktop GL: request an OpenGL ES 2.0 context. */
	struct st_context *st = st_create_context(API_OPENGLES2, pipe, &visual,
	                                          NULL, &opts, 0, 0);
	if (!st) { printf("gles: st_create_context(API_OPENGLES2) NULL\n"); return 1; }
	printf("gles: ES2 context created\n");

	_mesa_make_current(st->ctx, NULL, NULL);
	const char *ver = (const char *)glGetString(GL_VERSION);
	const char *sl  = (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION);
	printf("gles: made current; GL_VERSION=%s ; GLSL=%s\n",
	       ver ? ver : "(null)", sl ? sl : "(null)");

	struct pipe_resource rtt = { 0 };
	rtt.target = PIPE_TEXTURE_2D; rtt.format = PIPE_FORMAT_R8G8B8A8_UNORM;
	rtt.width0 = 256; rtt.height0 = 256; rtt.depth0 = 1; rtt.array_size = 1;
	rtt.bind = PIPE_BIND_RENDER_TARGET | PIPE_BIND_SAMPLER_VIEW;
	struct pipe_resource *rt = pscreen->resource_create(pscreen, &rtt);
	if (!rt) { printf("gles: RT NULL\n"); return 1; }

	GLuint tex = 0, fbo = 0;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	st_context_teximage(st, GL_TEXTURE_2D, 0, PIPE_FORMAT_R8G8B8A8_UNORM, rt, 0);
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
	GLenum fbs = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	printf("gles: FBO status=0x%x (complete=0x%x)\n", fbs, GL_FRAMEBUFFER_COMPLETE);

	glViewport(0, 0, 256, 256);
	glClearColor(0.0f, 1.0f, 0.0f, 1.0f);  /* green */
	glClear(GL_COLOR_BUFFER_BIT);
	glFinish();
	printf("gles: glClear+glFinish done\n");

	struct pipe_box box = { 0 };
	box.width = 256; box.height = 256; box.depth = 1;
	struct pipe_transfer *xfer = NULL;
	void *map = pipe->texture_map(pipe, rt, 0, PIPE_MAP_READ, &box, &xfer);
	if (map) {
		uint32_t px = ((volatile uint32_t *)map)[128 * 256 + 128];
		printf("gles: GLCLEAR readback center=0x%08x (expect green 0xff00ff00)\n", px);
		pipe->texture_unmap(pipe, xfer);
	}
	printf("gles: GLES-SMOKE-DONE (Mesa OpenGL ES clear on the V3D)\n");
	return 0;
}
