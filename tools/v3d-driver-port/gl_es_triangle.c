/*
 * gl_es_triangle.c — OpenGL ES shader-pipeline proof on the V3D. Builds on
 * gl_es_smoke.c (API_OPENGLES2 context + surfaceless FBO) but instead of a bare
 * glClear it runs the real ES pipeline: compile a GLSL-ES vertex + fragment
 * program, upload a triangle VBO, glVertexAttribPointer + glDrawArrays, then read
 * the RT back and check a pixel inside the triangle is the shader's color and a
 * corner is the clear color. ES has NO fixed-function glBegin/glEnd — this is the
 * VBO+GLSL-ES surface that yQuake2's gl3 renderer (and SuperTuxKart) require.
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
#include "main/menums.h"
#include "frontend/api.h"
#include "main/mtypes.h"
#include "state_tracker/st_context.h"
#define GL_GLEXT_PROTOTYPES 1
#include "GL/gl.h"
#include "GL/glext.h"

struct pipe_screen_config;
struct renderonly;
struct pipe_screen *v3d_screen_create(int fd, const struct pipe_screen_config *config, struct renderonly *ro);
extern unsigned char _mesa_make_current(struct gl_context *ctx,
                                        struct gl_framebuffer *drawFb,
                                        struct gl_framebuffer *readFb);

static const char *VS =
	"attribute vec2 pos;\n"
	"void main(){ gl_Position = vec4(pos, 0.0, 1.0); }\n";
static const char *FS =
	"precision mediump float;\n"
	"void main(){ gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); }\n";  /* red */

static GLuint compile(GLenum type, const char *src, const char *tag)
{
	GLuint s = glCreateShader(type);
	glShaderSource(s, 1, &src, NULL);
	glCompileShader(s);
	GLint ok = 0;
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	char log[512];
	log[0] = 0;
	glGetShaderInfoLog(s, sizeof(log), NULL, log);
	printf("gles-tri: %s compile ok=%d log=%s\n", tag, ok, log[0] ? log : "(none)");
	return ok ? s : 0;
}

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	struct pipe_screen_config cfg;
	memset(&cfg, 0, sizeof(cfg));
	struct pipe_screen *pscreen = v3d_screen_create(0, &cfg, NULL);
	if (!pscreen) { printf("gles-tri: pipe_screen NULL\n"); return 1; }
	struct pipe_context *pipe = pscreen->context_create(pscreen, NULL, 0);
	if (!pipe) { printf("gles-tri: pipe_context NULL\n"); return 1; }

	struct gl_config visual;
	struct st_config_options opts;
	memset(&visual, 0, sizeof(visual));
	memset(&opts, 0, sizeof(opts));
	struct st_context *st = st_create_context(API_OPENGLES2, pipe, &visual, NULL, &opts, 0, 0);
	if (!st) { printf("gles-tri: st_create_context NULL\n"); return 1; }
	_mesa_make_current(st->ctx, NULL, NULL);
	printf("gles-tri: GL_VERSION=%s\n", (const char *)glGetString(GL_VERSION));

	/* RT + FBO (same as gl_es_smoke). */
	struct pipe_resource rtt = { 0 };
	rtt.target = PIPE_TEXTURE_2D; rtt.format = PIPE_FORMAT_R8G8B8A8_UNORM;
	rtt.width0 = 256; rtt.height0 = 256; rtt.depth0 = 1; rtt.array_size = 1;
	rtt.bind = PIPE_BIND_RENDER_TARGET | PIPE_BIND_SAMPLER_VIEW;
	struct pipe_resource *rt = pscreen->resource_create(pscreen, &rtt);
	if (!rt) { printf("gles-tri: RT NULL\n"); return 1; }
	GLuint tex = 0, fbo = 0;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	st_context_teximage(st, GL_TEXTURE_2D, 0, PIPE_FORMAT_R8G8B8A8_UNORM, rt, 0);
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
	printf("gles-tri: FBO status=0x%x (complete=0x%x)\n",
	       glCheckFramebufferStatus(GL_FRAMEBUFFER), GL_FRAMEBUFFER_COMPLETE);

	/* ES shader program. */
	GLuint vs = compile(GL_VERTEX_SHADER, VS, "VS");
	GLuint fs = compile(GL_FRAGMENT_SHADER, FS, "FS");
	if (!vs || !fs) { printf("gles-tri: FAIL shader compile\n"); return 1; }
	GLuint prog = glCreateProgram();
	glAttachShader(prog, vs);
	glAttachShader(prog, fs);
	glBindAttribLocation(prog, 0, "pos");
	glLinkProgram(prog);
	GLint linked = 0;
	glGetProgramiv(prog, GL_LINK_STATUS, &linked);
	char plog[512]; plog[0] = 0; glGetProgramInfoLog(prog, sizeof(plog), NULL, plog);
	printf("gles-tri: program link ok=%d log=%s\n", linked, plog[0] ? plog : "(none)");
	if (!linked) { printf("gles-tri: FAIL link\n"); return 1; }
	glUseProgram(prog);

	/* Triangle VBO covering the center. */
	static const float verts[6] = { -0.6f, -0.6f,  0.6f, -0.6f,  0.0f, 0.6f };
	GLuint vbo = 0;
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void *)0);

	glViewport(0, 0, 256, 256);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);   /* black */
	glClear(GL_COLOR_BUFFER_BIT);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glFinish();
	printf("gles-tri: draw done (GL error=0x%x)\n", glGetError());

	/* Readback: center should be red (0xff0000ff), a corner clear-black (0xff000000). */
	struct pipe_box box = { 0 };
	box.width = 256; box.height = 256; box.depth = 1;
	struct pipe_transfer *xfer = NULL;
	uint32_t center = 0, corner = 0;
	void *map = pipe->texture_map(pipe, rt, 0, PIPE_MAP_READ, &box, &xfer);
	if (map) {
		center = ((volatile uint32_t *)map)[128 * 256 + 128];
		corner = ((volatile uint32_t *)map)[8 * 256 + 8];
		pipe->texture_unmap(pipe, xfer);
	}
	int pass = (center == 0xff0000ffu) && (corner == 0xff000000u);
	printf("gles-tri: center=0x%08x (expect red 0xff0000ff) corner=0x%08x (expect black 0xff000000) => %s\n",
	       center, corner, pass ? "PASS" : "FAIL");
	printf("gles-tri: GLES-TRIANGLE-DONE\n");
	return 0;
}
