/*
 * gl_frontend_smoke.c — GLQuake Path-C Phase-4 link-drive: wrap the v3d
 * pipe_context in a Mesa GL context (st_create_context) and call a GL entry,
 * to enumerate the GL runtime symbol closure (GL dispatch / _mesa_* / shared
 * glapi) when linking libGL-phoenix.a + libv3d-phoenix.a.
 *
 * Correctness (current-context, framebuffer) comes after the link resolves; this
 * is the link-drive smoke, modeled on how the v3d driver closure was de-risked.
 *
 * Copyright 2026 Phoenix Systems  %LICENSE%
 */
#include <stdio.h>
#include <string.h>
#include "pipe/p_screen.h"
#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "main/menums.h"                 /* gl_api: API_OPENGL_COMPAT */
#include "frontend/api.h"                /* struct st_config_options */
#include "main/mtypes.h"                 /* struct gl_config */
#include "state_tracker/st_context.h"    /* st_create_context */
#include "GL/gl.h"                        /* glClear, GL_COLOR_BUFFER_BIT */

struct pipe_screen_config;
struct renderonly;
struct pipe_screen *v3d_screen_create(int fd,
                                      const struct pipe_screen_config *config,
                                      struct renderonly *ro);

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	struct pipe_screen_config cfg;
	memset(&cfg, 0, sizeof(cfg));
	struct pipe_screen *pscreen = v3d_screen_create(0, &cfg, NULL);
	if (!pscreen) { printf("gl-smoke: pipe_screen NULL\n"); return 1; }
	struct pipe_context *pipe = pscreen->context_create(pscreen, NULL, 0);
	if (!pipe) { printf("gl-smoke: pipe_context NULL\n"); return 1; }

	struct gl_config visual;
	struct st_config_options opts;
	memset(&visual, 0, sizeof(visual));
	memset(&opts, 0, sizeof(opts));
	struct st_context *st = st_create_context(API_OPENGL_COMPAT, pipe, &visual,
	                                          NULL, &opts, false, false);
	printf("gl-smoke: st_create_context -> %p\n", (void *)st);

	glClear(GL_COLOR_BUFFER_BIT);
	printf("gl-smoke: glClear returned; GL frontend linked\n");
	return 0;
}
