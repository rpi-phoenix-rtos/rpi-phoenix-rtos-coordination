/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * gl_uif_probe.c — V3D UIF_XOR tiling isolation probe.
 *
 * Root-causes the quake3 q3dm7 "black lightmap" bug (and the same read-side class as
 * quake2 floor-speckle + vkQuake striping): a merged lightmap atlas renders correct at
 * 512x512 (q3dm1) but BLACK/corrupt at 1024x1024 (q3dm7). Prior analysis established:
 *   - both 512 and 1024 atlases are UIF_XOR tiled;
 *   - our winsys uif_pixel_off ≡ Mesa's v3d_get_uif_pixel_offset EXACTLY (store tiler correct);
 *   - the UIF_XOR offset is a pure fn of (cpp,image_h,x,y) — NO UIFCFG/devinfo input, so the
 *     hardcoded UIFCFG=0x45 is NOT implicated;
 *   - GL uploads CPU-tile in-driver (Mesa), not via the winsys TFU.
 * => the bug is SAMPLING/DESCRIPTOR-side. This probe makes the store-vs-sample split that
 * source-staring could not, by reading a known-pattern texture back TWO ways:
 *   (A) STORE side  — glGetTexImage (CPU transfer, uses uif_pixel_off untile).
 *   (B) SAMPLE side — render a 1:1 NEAREST/REPLACE quad + glReadPixels (uses the TMU +
 *       the texture-shader-state descriptor).
 * A correct + B correct at 512 but (A correct, B wrong) at 1024 ⇒ the TMU texture-shader-state
 * DESCRIPTOR (v3dx_state.c v3d_setup_texture_shader_state) mis-encodes some height/level-pitch
 * field at the >512 threshold (a bitfield that overflows at 1024). That is the localized
 * diagnosis to hand to the read-side fix (which may be owner-attended).
 *
 * Design (per advisor):
 *   - Build each atlas exactly like quake: many 128x128 glTexSubImage2D SUB-IMAGES at non-zero
 *     offsets (exercises the box-offset store path, not just the aligned whole-upload).
 *   - Each texel encodes its own (x,y) in RGBA8, recoverable for coords < 4096.
 *   - GL_NEAREST + GL_REPLACE + texel-center 1:1 quad + viewport == texture size, so a correct
 *     pipeline yields sample[px,py] == texel(px,py) exactly (no filtering noise).
 *   - A/B 512 (known-good) vs 1024 (known-bad) in the SAME binary, everything else identical.
 *
 * Boilerplate (powerOn/screen/context/FBO) mirrors gl_det_harness.c. Standalone binary.
 *
 * BUILD (reconstruct gl-det-build.sh): cross-compile against the Mesa GL include set
 * (see build-gl-phoenix.py GL_DIRS + INCLUDES) and link the two folded libs
 *   tools/.gpu-libs/libGL-phoenix.a + libv3d-phoenix.a  (+ libphoenix, static, aarch64).
 * Deploy to the netboot export /bin/gl-uif and run at psh; capture UART.
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
void v3d_phoenix_powerOn(void);

#define BLK 128            /* quake lightmap block size (the sub-image unit) */
#define MAXSZ 1024

/* Encode a texel's global (x,y) into RGBA8; recoverable for x,y < 4096 (10 bits used). */
static inline void enc(unsigned char *p, unsigned x, unsigned y)
{
	p[0] = (unsigned char)(x & 0xff);
	p[1] = (unsigned char)(y & 0xff);
	p[2] = (unsigned char)(((x >> 8) & 0x0f) | (((y >> 8) & 0x0f) << 4));
	p[3] = 0xff;
}
static inline int dec_ok(const unsigned char *p, unsigned x, unsigned y)
{
	return p[0] == (unsigned char)(x & 0xff) &&
	       p[1] == (unsigned char)(y & 0xff) &&
	       p[2] == (unsigned char)(((x >> 8) & 0x0f) | (((y >> 8) & 0x0f) << 4));
}

/* Scan a size*size RGBA8 buffer; report mismatch count + first mismatch + the set of
 * distinct 128-block rows that contain any mismatch (localizes the corruption). */
static unsigned scan(const char *tag, unsigned size, const unsigned char *buf)
{
	unsigned mism = 0, first_x = 0, first_y = 0;
	uint64_t badrows = 0;   /* bit b set => block-row b has a mismatch (size/128 <= 8 rows) */
	for (unsigned y = 0; y < size; y++) {
		for (unsigned x = 0; x < size; x++) {
			const unsigned char *p = buf + ((size_t)y * size + x) * 4;
			if (!dec_ok(p, x, y)) {
				if (mism == 0) { first_x = x; first_y = y; }
				mism++;
				badrows |= (uint64_t)1 << (y / BLK);
			}
		}
	}
	printf("gl-uif:   %s: mismatches=%u/%u", tag, mism, size * size);
	if (mism) {
		const unsigned char *fp = buf + ((size_t)first_y * size + first_x) * 4;
		printf("  first@(%u,%u) got RGBA=%02x%02x%02x%02x  bad-blockrows=0x%llx",
		       first_x, first_y, fp[0], fp[1], fp[2], fp[3],
		       (unsigned long long)badrows);
	}
	printf("\n");
	return mism;
}

static void probe_size(unsigned size)
{
	printf("gl-uif: === atlas %ux%u (UIF_XOR expected) ===\n", size, size);

	unsigned char *blk = (unsigned char *)malloc((size_t)BLK * BLK * 4);
	unsigned char *back = (unsigned char *)malloc((size_t)size * size * 4);

	GLuint tex = 0;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	/* allocate the full level first, then fill via sub-images at offsets (quake atlas build) */
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	for (unsigned by = 0; by < size; by += BLK) {
		for (unsigned bx = 0; bx < size; bx += BLK) {
			for (unsigned j = 0; j < BLK; j++)
				for (unsigned i = 0; i < BLK; i++)
					enc(blk + (j * BLK + i) * 4, bx + i, by + j);
			glTexSubImage2D(GL_TEXTURE_2D, 0, bx, by, BLK, BLK,
			                GL_RGBA, GL_UNSIGNED_BYTE, blk);
		}
	}
	glFinish();

	/* (A) STORE side: CPU transfer readback via uif_pixel_off untile. */
	memset(back, 0, (size_t)size * size * 4);
	glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, back);
	unsigned store_mism = scan("STORE (glGetTexImage / uif_pixel_off)", size, back);

	/* (B) SAMPLE side: render a 1:1 NEAREST/REPLACE quad, then glReadPixels. Output pixel
	 * (px,py) samples texcoord ((px+0.5)/size,(py+0.5)/size) => texel (px,py) under NEAREST. */
	glViewport(0, 0, size, size);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_TEXTURE_2D);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glBegin(GL_TRIANGLES);
	glTexCoord2f(0.0f, 0.0f); glVertex3f(-1.0f, -1.0f, 0.0f);
	glTexCoord2f(1.0f, 0.0f); glVertex3f( 1.0f, -1.0f, 0.0f);
	glTexCoord2f(1.0f, 1.0f); glVertex3f( 1.0f,  1.0f, 0.0f);
	glTexCoord2f(0.0f, 0.0f); glVertex3f(-1.0f, -1.0f, 0.0f);
	glTexCoord2f(1.0f, 1.0f); glVertex3f( 1.0f,  1.0f, 0.0f);
	glTexCoord2f(0.0f, 1.0f); glVertex3f(-1.0f,  1.0f, 0.0f);
	glEnd();
	glFinish();
	memset(back, 0, (size_t)size * size * 4);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE, back);
	unsigned sample_mism = scan("SAMPLE (TMU render+glReadPixels)", size, back);

	printf("gl-uif: VERDICT %ux%u: store=%s sample=%s => %s\n", size, size,
	       store_mism ? "CORRUPT" : "clean",
	       sample_mism ? "CORRUPT" : "clean",
	       (!store_mism && sample_mism) ? "TMU-DESCRIPTOR (read-side) bug"
	       : (store_mism && !sample_mism) ? "STORE/uif_pixel_off bug"
	       : (store_mism && sample_mism) ? "BOTH sides corrupt"
	       : "clean (no repro at this size)");

	glDeleteTextures(1, &tex);
	free(blk);
	free(back);
}

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	printf("gl-uif: start (V3D UIF_XOR store-vs-sample isolation, 512 vs 1024)\n");
	v3d_phoenix_powerOn();

	struct pipe_screen_config cfg;
	memset(&cfg, 0, sizeof(cfg));
	struct pipe_screen *pscreen = v3d_screen_create(0, &cfg, NULL);
	if (!pscreen) { printf("gl-uif: pipe_screen NULL\n"); return 1; }
	struct pipe_context *pipe = pscreen->context_create(pscreen, NULL, 0);
	if (!pipe) { printf("gl-uif: pipe_context NULL\n"); return 1; }
	struct gl_config visual; struct st_config_options opts;
	memset(&visual, 0, sizeof(visual)); memset(&opts, 0, sizeof(opts));
	struct st_context *st = st_create_context(API_OPENGL_COMPAT, pipe, &visual, NULL, &opts, 0, 0);
	if (!st) { printf("gl-uif: st_create_context NULL\n"); return 1; }
	_mesa_make_current(st->ctx, NULL, NULL);
	printf("gl-uif: GL up; %s / %s\n", (const char *)glGetString(GL_VERSION),
	       (const char *)glGetString(GL_RENDERER));

	/* A DRAM RGBA8 FBO sized to the largest atlas; glReadPixels reads its CPU-mapped BO. */
	GLuint fbo = 0, rb = 0;
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glGenRenderbuffers(1, &rb);
	glBindRenderbuffer(GL_RENDERBUFFER, rb);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, MAXSZ, MAXSZ);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
	GLenum fbs = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	printf("gl-uif: FBO %dx%d status=0x%x (complete=0x%x)\n", MAXSZ, MAXSZ, fbs, GL_FRAMEBUFFER_COMPLETE);
	if (fbs != GL_FRAMEBUFFER_COMPLETE) { printf("gl-uif: FBO incomplete ABORT\n"); return 2; }

	probe_size(512);    /* known-good (q3dm1) */
	probe_size(1024);   /* known-bad  (q3dm7) */

	printf("gl-uif: PASS\n");
	return 0;
}
