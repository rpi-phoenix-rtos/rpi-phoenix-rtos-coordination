/*
 * Phoenix-RTOS
 *
 * mipcheck: are glGenerateMipmap's levels right on V3D, for power-of-two and other sizes?
 *
 * For each size and format: upload a smooth pattern to level 0, glGenerateMipmap, then read
 * every level back twice and compare it with a CPU reference (repeated 2x2 box filter, each
 * level floor(w/2) x floor(h/2), at least 1, the odd last row/column left out as the V3D TFU
 * does):
 *   fbo      the level attached to a framebuffer and read with glReadPixels (the render path's
 *            view of the level's memory);
 *   sampler  the level drawn through the texture unit (textureLod, nearest) into an RGBA8
 *            framebuffer and read back (the TMU's view: a level the TFU wrote in a layout the
 *            TMU does not expect reads back scrambled here).
 * The pattern is smooth, so filters that round or weigh differently stay within a few code
 * values; a misplaced tile is off by tens. Levels of 1-2 texels per side are reported but not
 * graded (the TFU drops the odd row/column, the reference cannot follow every convention).
 *
 * Output: one MIPCHECK line per size/format/level/path, then "MIPCHECK result=PASS|FAIL".
 * V3D_NPOT_MIPGEN=tfu|render (read by our mesa_drm patch 0004, when it supports it) selects
 * the NPOT path inside Mesa; this program only reports what it reads.
 *
 * Copyright 2026 Phoenix Systems
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#define GRADE_LIMIT 16 /* max |error| per channel for a graded level */

typedef struct {
	int w, h;
} size2_t;

static const size2_t sizes[] = {
	{ 256, 256 }, { 128, 64 }, { 64, 64 },           /* power of two */
	{ 260, 195 }, { 288, 195 }, { 296, 164 },         /* Quake II model skins */
	{ 255, 255 }, { 257, 129 }, { 300, 200 }, { 100, 60 }, { 63, 37 }, { 1000, 700 }, { 17, 9 },
};


static unsigned char pattern(int x, int y, int c, int w, int h)
{
	double u = (double)x / w, v = (double)y / h;
	double f = 0.5 + 0.25 * sin(6.2831853 * (u * (1.5 + c) + v * 0.7)) + 0.25 * cos(6.2831853 * (v * (1.2 + 0.5 * c) - u * 0.4));

	return (unsigned char)lrint(255.0 * f);
}


/* One box-filter step, 4 channels, floor sizes (the odd last row/column is left out) */
static void downsample(const unsigned char *src, int w, int h, unsigned char *dst, int *ow, int *oh)
{
	int nw = (w > 1) ? (w / 2) : 1, nh = (h > 1) ? (h / 2) : 1, x, y, c;

	for (y = 0; y < nh; y++) {
		for (x = 0; x < nw; x++) {
			int x0 = (w > 1) ? (2 * x) : 0, x1 = (w > 1) ? (2 * x + 1) : 0;
			int y0 = (h > 1) ? (2 * y) : 0, y1 = (h > 1) ? (2 * y + 1) : 0;

			for (c = 0; c < 4; c++) {
				int s = src[(y0 * w + x0) * 4 + c] + src[(y0 * w + x1) * 4 + c] + src[(y1 * w + x0) * 4 + c] + src[(y1 * w + x1) * 4 + c];
				dst[(y * nw + x) * 4 + c] = (unsigned char)((s + 2) / 4);
			}
		}
	}
	*ow = nw;
	*oh = nh;
}


static GLuint compile(GLenum type, const char *src)
{
	GLuint s = glCreateShader(type);
	GLint ok = 0;

	glShaderSource(s, 1, &src, NULL);
	glCompileShader(s);
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[512];
		glGetShaderInfoLog(s, sizeof(log), NULL, log);
		printf("MIPCHECK shader error: %s\n", log);
		exit(2);
	}
	return s;
}


/* Compare a read-back level (RGBA8, w x h) with the reference; channels: 4 or 3 (alpha ignored) */
static int compare(const unsigned char *got, const unsigned char *ref, int w, int h, int nch, double *mean)
{
	long sum = 0;
	int x, c, max = 0;

	for (x = 0; x < w * h; x++) {
		for (c = 0; c < nch; c++) {
			int d = abs((int)got[x * 4 + c] - (int)ref[x * 4 + c]);
			sum += d;
			if (d > max) {
				max = d;
			}
		}
	}
	*mean = (double)sum / ((double)w * h * nch);
	return max;
}


int main(void)
{
	static const char *vs = "#version 300 es\n"
		"in vec2 p; out vec2 uv;\n"
		"void main() { uv = p * 0.5 + 0.5; gl_Position = vec4(p, 0.0, 1.0); }\n";
	static const char *fs = "#version 300 es\n"
		"precision highp float; uniform sampler2D t; uniform int lod; in vec2 uv; out vec4 o;\n"
		"void main() { o = textureLod(t, uv, float(lod)); }\n";
	static const GLfloat quad[] = { -1.f, -1.f, 1.f, -1.f, -1.f, 1.f, 1.f, 1.f };
	static const EGLint ca[] = { EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 0, EGL_NONE };
	EGLDisplay dpy;
	EGLContext ctx;
	EGLint major, minor;
	GLuint prog, vbo, fbo, rt, tex;
	GLint locLod, locP;
	unsigned char *ref, *nref, *got;
	int fails = 0, graded = 0, f;
	size_t si;
	const char *mode = getenv("V3D_NPOT_MIPGEN");

	dpy = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
	if ((dpy == EGL_NO_DISPLAY) || !eglInitialize(dpy, &major, &minor)) {
		printf("MIPCHECK result=FAIL why=egl-init err=0x%x\n", eglGetError());
		return 1;
	}
	eglBindAPI(EGL_OPENGL_ES_API);
	ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ca);
	if ((ctx == EGL_NO_CONTEXT) || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
		printf("MIPCHECK result=FAIL why=egl-context err=0x%x\n", eglGetError());
		return 1;
	}
	printf("MIPCHECK gl renderer=\"%s\" version=\"%s\" V3D_NPOT_MIPGEN=%s\n", (const char *)glGetString(GL_RENDERER),
		(const char *)glGetString(GL_VERSION), (mode != NULL) ? mode : "(unset)");

	prog = glCreateProgram();
	glAttachShader(prog, compile(GL_VERTEX_SHADER, vs));
	glAttachShader(prog, compile(GL_FRAGMENT_SHADER, fs));
	glLinkProgram(prog);
	glUseProgram(prog);
	locLod = glGetUniformLocation(prog, "lod");
	locP = glGetAttribLocation(prog, "p");
	glUniform1i(glGetUniformLocation(prog, "t"), 0);
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
	glVertexAttribPointer((GLuint)locP, 2, GL_FLOAT, GL_FALSE, 0, 0);
	glEnableVertexAttribArray((GLuint)locP);
	glGenFramebuffers(1, &fbo);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);

	ref = malloc(1000 * 700 * 4);
	nref = malloc(1000 * 700 * 4);
	got = malloc(1000 * 700 * 4);
	if ((ref == NULL) || (nref == NULL) || (got == NULL)) {
		printf("MIPCHECK result=FAIL why=oom\n");
		return 1;
	}

	for (f = 0; f < 2; f++) {
		const char *fname = (f == 0) ? "RGBA8" : "RGB8";
		int nch = (f == 0) ? 4 : 3;

		for (si = 0; si < sizeof(sizes) / sizeof(sizes[0]); si++) {
			int w = sizes[si].w, h = sizes[si].h, lw, lh, level, x, y, c;
			unsigned char *up = malloc((size_t)w * h * 4);

			for (y = 0; y < h; y++) {
				for (x = 0; x < w; x++) {
					for (c = 0; c < 4; c++) {
						ref[(y * w + x) * 4 + c] = (c < nch) ? pattern(x, y, c, w, h) : 255;
					}
					for (c = 0; c < nch; c++) {
						up[(y * w + x) * nch + c] = ref[(y * w + x) * 4 + c];
					}
				}
			}
			glGenTextures(1, &tex);
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, tex);
			glTexImage2D(GL_TEXTURE_2D, 0, (f == 0) ? GL_RGBA8 : GL_RGB8, w, h, 0, (f == 0) ? GL_RGBA : GL_RGB, GL_UNSIGNED_BYTE, up);
			free(up);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glGenerateMipmap(GL_TEXTURE_2D);
			glFinish();

			lw = w;
			lh = h;
			for (level = 0;; level++) {
				int p;

				for (p = 0; p < 2; p++) {
					double mean = 0.0;
					int max, gradeIt = (lw > 2) && (lh > 2) && (level > 0);

					glBindFramebuffer(GL_FRAMEBUFFER, fbo);
					if (p == 0) {
						glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, level);
						rt = 0;
					}
					else {
						glGenTextures(1, &rt);
						glBindTexture(GL_TEXTURE_2D, rt);
						glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, lw, lh);
						glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rt, 0);
						glBindTexture(GL_TEXTURE_2D, tex);
						glViewport(0, 0, lw, lh);
						glUniform1i(locLod, level);
						glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
					}
					if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
						printf("MIPCHECK %s %dx%d level=%d path=%s incomplete-framebuffer\n", fname, w, h, level, (p == 0) ? "fbo" : "sampler");
					}
					else {
						glReadPixels(0, 0, lw, lh, GL_RGBA, GL_UNSIGNED_BYTE, got);
						max = compare(got, ref, lw, lh, nch, &mean);
						if (gradeIt) {
							graded++;
							if (max > GRADE_LIMIT) {
								fails++;
							}
						}
						printf("MIPCHECK %s %dx%d level=%d %dx%d path=%s maxerr=%d meanerr=%.2f%s\n", fname, w, h, level, lw, lh,
							(p == 0) ? "fbo" : "sampler", max, mean, !gradeIt ? " (not graded)" : ((max > GRADE_LIMIT) ? " FAIL" : ""));
					}
					glBindFramebuffer(GL_FRAMEBUFFER, 0);
					if (rt != 0) {
						glDeleteTextures(1, &rt);
						glBindTexture(GL_TEXTURE_2D, tex);
					}
				}
				if ((lw == 1) && (lh == 1)) {
					break;
				}
				downsample(ref, lw, lh, nref, &lw, &lh);
				memcpy(ref, nref, (size_t)lw * lh * 4);
			}
			glDeleteTextures(1, &tex);
		}
	}

	printf("MIPCHECK result=%s graded=%d failed=%d limit=%d glerror=0x%x\n", (fails == 0) ? "PASS" : "FAIL", graded, fails,
		GRADE_LIMIT, glGetError());
	return (fails == 0) ? 0 : 1;
}
