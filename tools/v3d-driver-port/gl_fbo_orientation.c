/*
 * gl_fbo_orientation.c — does an RTT -> scanout-sized quad blit preserve orientation?
 *
 * This answers the one question that decides whether the planned "real scanout
 * predicate" fix is correct at all:
 * docs/misc/2026-09-08-flipy-scanout-gate-work-order.md
 *
 * Background. st_atom_framebuffer.c forces Y_0_TOP for any FBO >= 1024x768 and
 * leaves smaller ones Y_0_BOTTOM. A CORRECT scanout predicate would produce
 * exactly that split for SuperTuxKart with scale_rtts_factor < 0.711: the
 * 1920x1080 scanout FBO is Y_0_TOP, the scaled deferred RTT is Y_0_BOTTOM. That
 * is also the configuration in which STK currently renders UPSIDE DOWN, so the
 * planned fix cannot work by "making them agree" -- it works only if the state
 * tracker compensates for the orientation difference across the RTT -> scanout
 * pass. Nobody has measured whether it does.
 *
 * No Mesa change is needed to find out, because today's gate already yields the
 * mismatched pair: pick a sub-1024 RTT and a 1920x1080 destination.
 *
 * Method. Three steps, all read back on the CPU (no HDMI, prints its own verdict):
 *
 *   A. calibrate SMALL (960x540, Y_0_BOTTOM): draw a red band over the bottom
 *      half in NDC (y -1..0) and record which MEMORY rows it occupies.
 *   B. calibrate LARGE (1920x1080, Y_0_TOP): same NDC band, same question.
 *      A vs B shows numerically what the size gate does.
 *   C. the decisive one: render the band into SMALL, then draw SMALL as a
 *      full-screen TEXTURED QUAD into LARGE with the standard
 *      uv = (ndc + 1) / 2 mapping, and record the band's memory rows in LARGE.
 *
 * Verdict: C is compared against B. B is "the band drawn straight into the
 * destination"; C is "the same band routed through an RTT". If they land on the
 * same rows the round trip is orientation-preserving and the predicate route is
 * sound; if they land on opposite ends, a correct predicate would flip STK and
 * the GL_MESA_framebuffer_flip_y alternative is the one to take.
 *
 * A textured quad is used deliberately rather than glBlitFramebuffer: st handles
 * orientation differently for a blit than for a shader draw, and STK's
 * renderPassThrough is a quad. Testing the blit would answer a question nobody
 * asked.
 *
 * Copyright 2026 Phoenix Systems
 * SPDX-License-Identifier: BSD-3-Clause
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

#include "GL/gl.h"
#include "GL/glext.h"

struct pipe_screen_config;
struct renderonly;
struct pipe_screen *v3d_screen_create(int fd, const struct pipe_screen_config *config, struct renderonly *ro);
extern unsigned char _mesa_make_current(struct gl_context *ctx,
                                        struct gl_framebuffer *drawFb,
                                        struct gl_framebuffer *readFb);

#define SMALL_W 960
#define SMALL_H 540
#define LARGE_W 1920
#define LARGE_H 1080

/* Flat red, for the calibration draws. */
static const char *VS_FLAT =
	"attribute vec2 pos;\n"
	"void main(){ gl_Position = vec4(pos, 0.0, 1.0); }\n";
static const char *FS_FLAT =
	"precision mediump float;\n"
	"void main(){ gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); }\n";

/* Textured pass-through, mirroring what STK's renderPassThrough does. */
static const char *VS_TEX =
	"attribute vec2 pos;\n"
	"varying vec2 uv;\n"
	"void main(){ uv = (pos + 1.0) * 0.5; gl_Position = vec4(pos, 0.0, 1.0); }\n";
static const char *FS_TEX =
	"precision mediump float;\n"
	"varying vec2 uv;\n"
	"uniform sampler2D src;\n"
	"void main(){ gl_FragColor = texture2D(src, uv); }\n";

static GLuint compile(GLenum type, const char *src, const char *tag)
{
	GLuint s = glCreateShader(type);
	GLint ok = 0;
	char log[512];

	glShaderSource(s, 1, &src, NULL);
	glCompileShader(s);
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	log[0] = 0;
	glGetShaderInfoLog(s, sizeof(log), NULL, log);
	if (ok == 0) {
		printf("fboorient: %s compile FAILED log=%s\n", tag, log[0] ? log : "(none)");
	}
	return (ok != 0) ? s : 0u;
}

static GLuint program(const char *vs_src, const char *fs_src, int with_uv)
{
	GLuint vs = compile(GL_VERTEX_SHADER, vs_src, "VS");
	GLuint fs = compile(GL_FRAGMENT_SHADER, fs_src, "FS");
	GLuint p;
	GLint linked = 0;

	if ((vs == 0u) || (fs == 0u)) {
		return 0u;
	}
	p = glCreateProgram();
	glAttachShader(p, vs);
	glAttachShader(p, fs);
	glBindAttribLocation(p, 0, "pos");
	(void)with_uv;
	glLinkProgram(p);
	glGetProgramiv(p, GL_LINK_STATUS, &linked);
	if (linked == 0) {
		char plog[512];
		plog[0] = 0;
		glGetProgramInfoLog(p, sizeof(plog), NULL, plog);
		printf("fboorient: link FAILED log=%s\n", plog[0] ? plog : "(none)");
		return 0u;
	}
	return p;
}

/* Which MEMORY rows of `rt` contain red? Returns 0 on success. */
static int redRows(struct pipe_context *pipe, struct pipe_resource *rt,
                   unsigned int w, unsigned int h, int *firstRow, int *lastRow, unsigned int *nRows)
{
	struct pipe_box box = { 0 };
	struct pipe_transfer *xfer = NULL;
	void *map;
	unsigned int y, x;

	box.width = (int)w;
	box.height = (int)h;
	box.depth = 1;

	map = pipe->texture_map(pipe, rt, 0, PIPE_MAP_READ, &box, &xfer);
	if (map == NULL) {
		return -1;
	}

	*firstRow = -1;
	*lastRow = -1;
	*nRows = 0u;

	/* texture_map hands back a linear staging copy with stride == w*4 for these
	 * sizes; sample the row centre to avoid any edge/AA ambiguity. */
	for (y = 0u; y < h; y++) {
		const volatile uint32_t *row = &((volatile uint32_t *)map)[(size_t)y * w];
		unsigned int red = 0u;
		for (x = w / 4u; x < (3u * w) / 4u; x += (w / 8u)) {
			/* R8G8B8A8_UNORM little-endian: red is 0xff0000ff. */
			if (row[x] == 0xff0000ffu) {
				red++;
			}
		}
		if (red >= 2u) {
			if (*firstRow < 0) {
				*firstRow = (int)y;
			}
			*lastRow = (int)y;
			(*nRows)++;
		}
	}
	pipe->texture_unmap(pipe, xfer);
	return 0;
}

int main(void)
{
	struct pipe_screen_config cfg;
	struct pipe_screen *pscreen;
	struct pipe_context *pipe;
	struct gl_config visual;
	struct st_config_options opts;
	struct st_context *st;
	struct pipe_resource tmpl = { 0 };
	struct pipe_resource *rtSmall, *rtLarge;
	GLuint texSmall = 0, texLarge = 0, fboSmall = 0, fboLarge = 0;
	GLuint progFlat, progTex, vbo = 0;
	int fA = -1, lA = -1, fB = -1, lB = -1, fC = -1, lC = -1;
	unsigned int nA = 0u, nB = 0u, nC = 0u;

	/* Bottom half in NDC: y from -1 to 0. */
	static const float bandVerts[8] = {
		-1.0f, -1.0f,   1.0f, -1.0f,   -1.0f, 0.0f,   1.0f, 0.0f
	};
	/* Full screen. */
	static const float fullVerts[8] = {
		-1.0f, -1.0f,   1.0f, -1.0f,   -1.0f, 1.0f,   1.0f, 1.0f
	};

	setvbuf(stdout, NULL, _IONBF, 0);
	memset(&cfg, 0, sizeof(cfg));
	pscreen = v3d_screen_create(0, &cfg, NULL);
	if (pscreen == NULL) {
		printf("fboorient: pipe_screen NULL\n");
		return 1;
	}
	pipe = pscreen->context_create(pscreen, NULL, 0);
	if (pipe == NULL) {
		printf("fboorient: pipe_context NULL\n");
		return 1;
	}
	memset(&visual, 0, sizeof(visual));
	memset(&opts, 0, sizeof(opts));
	st = st_create_context(API_OPENGLES2, pipe, &visual, NULL, &opts, 0, 0);
	if (st == NULL) {
		printf("fboorient: st_create_context NULL\n");
		return 1;
	}
	_mesa_make_current(st->ctx, NULL, NULL);
	printf("fboorient: GL_VERSION=%s\n", (const char *)glGetString(GL_VERSION));
	printf("fboorient: SMALL=%dx%d (below the 1024x768 gate -> expect Y_0_BOTTOM)\n", SMALL_W, SMALL_H);
	printf("fboorient: LARGE=%dx%d (at/above the gate -> expect Y_0_TOP)\n", LARGE_W, LARGE_H);

	tmpl.target = PIPE_TEXTURE_2D;
	tmpl.format = PIPE_FORMAT_R8G8B8A8_UNORM;
	tmpl.depth0 = 1;
	tmpl.array_size = 1;
	tmpl.bind = PIPE_BIND_RENDER_TARGET | PIPE_BIND_SAMPLER_VIEW;

	tmpl.width0 = SMALL_W;
	tmpl.height0 = SMALL_H;
	rtSmall = pscreen->resource_create(pscreen, &tmpl);
	tmpl.width0 = LARGE_W;
	tmpl.height0 = LARGE_H;
	rtLarge = pscreen->resource_create(pscreen, &tmpl);
	if ((rtSmall == NULL) || (rtLarge == NULL)) {
		printf("fboorient: resource_create NULL\n");
		return 1;
	}

	glGenTextures(1, &texSmall);
	glBindTexture(GL_TEXTURE_2D, texSmall);
	st_context_teximage(st, GL_TEXTURE_2D, 0, PIPE_FORMAT_R8G8B8A8_UNORM, rtSmall, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glGenFramebuffers(1, &fboSmall);
	glBindFramebuffer(GL_FRAMEBUFFER, fboSmall);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texSmall, 0);
	printf("fboorient: SMALL FBO status=0x%x\n", glCheckFramebufferStatus(GL_FRAMEBUFFER));

	glGenTextures(1, &texLarge);
	glBindTexture(GL_TEXTURE_2D, texLarge);
	st_context_teximage(st, GL_TEXTURE_2D, 0, PIPE_FORMAT_R8G8B8A8_UNORM, rtLarge, 0);
	glGenFramebuffers(1, &fboLarge);
	glBindFramebuffer(GL_FRAMEBUFFER, fboLarge);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texLarge, 0);
	printf("fboorient: LARGE FBO status=0x%x\n", glCheckFramebufferStatus(GL_FRAMEBUFFER));

	progFlat = program(VS_FLAT, FS_FLAT, 0);
	progTex = program(VS_TEX, FS_TEX, 1);
	if ((progFlat == 0u) || (progTex == 0u)) {
		printf("fboorient: FAIL shaders\n");
		return 1;
	}
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void *)0);

	/* ---- A: band straight into SMALL ---- */
	glBindFramebuffer(GL_FRAMEBUFFER, fboSmall);
	glViewport(0, 0, SMALL_W, SMALL_H);
	glUseProgram(progFlat);
	glBufferData(GL_ARRAY_BUFFER, sizeof(bandVerts), bandVerts, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void *)0);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glFinish();
	(void)redRows(pipe, rtSmall, SMALL_W, SMALL_H, &fA, &lA, &nA);
	printf("fboorient: [A] NDC bottom-half band drawn into SMALL -> memory rows %d..%d (%u of %d)\n",
	       fA, lA, nA, SMALL_H);

	/* ---- B: same band straight into LARGE ---- */
	glBindFramebuffer(GL_FRAMEBUFFER, fboLarge);
	glViewport(0, 0, LARGE_W, LARGE_H);
	glUseProgram(progFlat);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glFinish();
	(void)redRows(pipe, rtLarge, LARGE_W, LARGE_H, &fB, &lB, &nB);
	printf("fboorient: [B] NDC bottom-half band drawn into LARGE -> memory rows %d..%d (%u of %d)\n",
	       fB, lB, nB, LARGE_H);

	/* ---- C: band into SMALL, then SMALL textured onto LARGE ---- */
	glBindFramebuffer(GL_FRAMEBUFFER, fboSmall);
	glViewport(0, 0, SMALL_W, SMALL_H);
	glUseProgram(progFlat);
	glBufferData(GL_ARRAY_BUFFER, sizeof(bandVerts), bandVerts, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void *)0);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glFinish();

	glBindFramebuffer(GL_FRAMEBUFFER, fboLarge);
	glViewport(0, 0, LARGE_W, LARGE_H);
	glUseProgram(progTex);
	glUniform1i(glGetUniformLocation(progTex, "src"), 0);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, texSmall);
	glBufferData(GL_ARRAY_BUFFER, sizeof(fullVerts), fullVerts, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void *)0);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glFinish();
	(void)redRows(pipe, rtLarge, LARGE_W, LARGE_H, &fC, &lC, &nC);
	printf("fboorient: [C] band via SMALL RTT, textured quad onto LARGE -> memory rows %d..%d (%u of %d)\n",
	       fC, lC, nC, LARGE_H);
	printf("fboorient: GL error=0x%x\n", glGetError());

	/* Verdict. B is the band drawn straight into the destination; C is the same
	 * band routed through the mismatched-orientation RTT. Same half => the round
	 * trip preserves orientation. */
	if ((nB == 0u) || (nC == 0u)) {
		printf("fboorient: INCONCLUSIVE (a calibration draw produced no red)\n");
	}
	else {
		int bTop = (fB < (LARGE_H / 2));
		int cTop = (fC < (LARGE_H / 2));
		printf("fboorient: B is in the %s half, C is in the %s half\n",
		       bTop ? "TOP" : "BOTTOM", cTop ? "TOP" : "BOTTOM");
		if (bTop == cTop) {
			printf("fboorient: VERDICT=PRESERVED — st compensates across the RTT->dest quad, so a\n"
			       "           correct scanout predicate would NOT flip STK. Marker route is sound.\n");
		}
		else {
			printf("fboorient: VERDICT=FLIPPED — the mismatched pair inverts the image, so a correct\n"
			       "           scanout predicate WOULD flip STK. Take the GL_MESA_framebuffer_flip_y\n"
			       "           route instead (set FlipY explicitly per present layer).\n");
		}
	}
	printf("fboorient: FBO-ORIENTATION-DONE\n");
	return 0;
}
