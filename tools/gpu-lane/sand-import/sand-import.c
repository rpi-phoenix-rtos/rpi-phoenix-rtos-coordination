/*
 * Phoenix-RTOS
 *
 * sand-import: checkpoint 1 of the zero-copy video design
 * (docs/gpu-new-lane/M10b-video-zero-copy.md, section 7).
 *
 * Does the rpivid block's SAND128 output, written into a render-server BO, come out of
 * Mesa's EGL dma-buf import (DRM_FORMAT_NV12 + DRM_FORMAT_MOD_BROADCOM_SAND128_COL_HEIGHT)
 * exactly as the CPU de-tile (rpivid_sand8_to_planar) reads it, and what does the GPU
 * de-tile (v3d_sand8_blit) cost?
 *
 *   1. hevc_rpivid (the video_player port's FFmpeg) decodes the file up to picture N. Its
 *      SAND picture pool is redirected into V3D BOs (DRM_IOCTL_V3D_CREATE_BO on the render
 *      node) by --wrap of rpivid_geom / rpivid_dma_alloc_cached / rpivid_dma_free, in the
 *      single-buffer NV12_COL128 layout: column height C = H16 + H16/2 lines, luma at 0,
 *      chroma at H16 * 128, both column strides C * 128. The hwaccel's own de-tile then reads
 *      the BO with these strides, so the decoded frame proves the block wrote that layout.
 *   2. The BO holding picture N is found by de-tiling every BO on the CPU and comparing it
 *      with the decoded frame; that CPU de-tile is the reference for everything below.
 *   3. EGL (surfaceless, renderD128) imports the BO's dma-buf (/v3dbuf/<h>):
 *        Y plane as DRM_FORMAT_R8 and CbCr as DRM_FORMAT_GR88, SAND128(C): drawn into an
 *        RGBA8 FBO through samplerExternalOES, read back, compared EXACTLY (yuv arm);
 *        the whole picture as DRM_FORMAT_NV12, SAND128(C): YUV->RGB in Mesa, compared with
 *        a CPU BT.709 / BT.601 narrow-range conversion (nv12 arm, tolerance);
 *        import variants (pitch width / 128, the bare modifier with pitch = C).
 *   4. Timing (glFinish around N draws): the SAND import draw (= v3d_sand8_blit of both
 *      planes + the draw, re-done on every draw), an RGBA texture draw of the same size, the
 *      upload of the three planes with glTexSubImage2D (today's browser path), a LINEAR
 *      YUV420 import draw (path A), the CPU write of a planar picture into an uncached BO,
 *      and the CPU de-tile of the uncached BO.
 *
 * Every line starts with "ZC1 "; the last is "ZC1 result=PASS|FAIL ...".
 *
 *   sand-import [-n picture] [-i iterations] [-synthetic] [file]
 *
 * -synthetic (or no file) skips the decoder: a 1920x1080 test pattern is tiled into the BO
 * on the CPU, so the Mesa side can be checked without the block.
 *
 * Copyright 2026 Phoenix Systems
 *
 * This file is part of Phoenix-RTOS.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/mman.h>

#include <xf86drm.h>
#include <drm_fourcc.h>
#include "v3d_drm.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>

#include "rpivid_cmd.h"
#include "rpivid_hw.h"
#include "rpivid_sand.h"

#define MAX_BOS 48


/* ---- the BO pool (render server) ---- */

typedef struct {
	int used;
	uint32_t handle;
	uint8_t *cpu;
	uint64_t pa;
	size_t size;
	int contiguous;
} bo_t;

static struct {
	int rfd;
	bo_t bo[MAX_BOS];
	int nbo, ncontig, nfail;
	int zc;                        /* the wraps are active */
	int geom_set;
	uint32_t w, h, h16, colh;      /* colh = C, in lines */
	size_t luma_size, chroma_marker;
	uint64_t pa_min, pa_max;
	int npa_fail;                  /* BOs refused: no or wrong physical address */
	pthread_mutex_t lock;
} P = { .rfd = -1, .lock = PTHREAD_MUTEX_INITIALIZER };


static double now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6;
}


static int pa_check(const bo_t *b)
{
	volatile uint32_t *via_bo, *via_pa;
	void *q;
	size_t i, pages = b->size / 4096u;
	int bad = 0;

	for (i = 0; i < pages; i++) {
		via_bo = (volatile uint32_t *)(void *)(b->cpu + i * 4096u);
		*via_bo = 0x5a1d0000u ^ (uint32_t)i ^ (b->handle << 20);
	}
	__asm__ volatile("dsb sy" ::: "memory");
	q = mmap(NULL, b->size, PROT_READ, MAP_PHYSMEM | MAP_ANONYMOUS | MAP_UNCACHED, -1, (off_t)b->pa);
	if (q == MAP_FAILED) {
		printf("ZC1 pa_check map-failed errno=%d\n", errno);
		return -1;
	}
	for (i = 0; i < pages; i++) {
		via_pa = (volatile uint32_t *)(void *)((uint8_t *)q + i * 4096u);
		bad += (*via_pa != (0x5a1d0000u ^ (uint32_t)i ^ (b->handle << 20))) ? 1 : 0;
		*(volatile uint32_t *)(void *)(b->cpu + i * 4096u) = 0u;
	}
	munmap(q, b->size);
	return (bad == 0) ? 0 : -1;
}


static int bo_new(size_t size)
{
	struct drm_v3d_create_bo cb;
	struct drm_v3d_mmap_bo mb;
	bo_t *b;
	void *p;
	size_t i, pages;
	int k;

	for (k = 0; (k < MAX_BOS) && P.bo[k].used; k++) {
	}
	if (k == MAX_BOS) {
		return -ENOSPC;
	}
	size = (size + 4095u) & ~(size_t)4095u;
	memset(&cb, 0, sizeof(cb));
	cb.size = (uint32_t)size;
	if (drmIoctl(P.rfd, DRM_IOCTL_V3D_CREATE_BO, &cb) != 0) {
		return -errno;
	}
	memset(&mb, 0, sizeof(mb));
	mb.handle = cb.handle;
	if (drmIoctl(P.rfd, DRM_IOCTL_V3D_MMAP_BO, &mb) != 0) {
		return -errno;
	}
	p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, P.rfd, (off_t)mb.offset);
	if (p == MAP_FAILED) {
		return -errno;
	}
	b = &P.bo[k];
	memset(b, 0, sizeof(*b));
	b->used = 1;
	b->handle = cb.handle;
	b->cpu = p;
	b->size = size;
	b->pa = (uint64_t)va2pa(p);
	b->contiguous = 1;
	pages = size / 4096u;
	for (i = 1; i < pages; i++) {
		if ((uint64_t)va2pa((uint8_t *)p + i * 4096u) != b->pa + i * 4096u) {
			b->contiguous = 0;
			break;
		}
	}
	/* The block DMAs to b->pa: prove that address independently of va2pa before any use.
	 * A marker per page through the BO mapping must read back through a MAP_PHYSMEM mapping
	 * of the PA (the flags rpivid_hw.c's map_phys uses, without MAP_DEVICE). */
	if ((b->pa == 0u) || (b->pa == (uint64_t)(addr_t)-1) || !b->contiguous || (pa_check(b) != 0)) {
		printf("ZC1 bo refused handle=%u pa=0x%llx contiguous=%d\n", b->handle, (unsigned long long)b->pa, b->contiguous);
		P.npa_fail++;
		munmap(p, size);
		{
			struct drm_gem_close gc = { .handle = cb.handle };

			(void)drmIoctl(P.rfd, DRM_IOCTL_GEM_CLOSE, &gc);
		}
		memset(b, 0, sizeof(*b));
		return -EFAULT;
	}
	P.nbo++;
	P.ncontig += b->contiguous;
	if ((P.pa_min == 0u) || (b->pa < P.pa_min)) {
		P.pa_min = b->pa;
	}
	if (b->pa + size > P.pa_max) {
		P.pa_max = b->pa + size;
	}
	return k;
}


static int bo_dmabuf(const bo_t *b)
{
	int fd = -1;

	if (drmPrimeHandleToFD(P.rfd, b->handle, DRM_CLOEXEC, &fd) != 0) {
		return -errno;
	}
	return fd;
}


/* ---- the wraps around the hwaccel (rpivid_hevc.o's undefined references) ---- */

void __real_rpivid_geom(rpivid_geom_t *g, uint32_t width, uint32_t height, unsigned int bit_depth);
int __real_rpivid_dma_alloc_cached(rpivid_dma_t *d, size_t size);
void __real_rpivid_dma_free(rpivid_dma_t *d);


void __wrap_rpivid_geom(rpivid_geom_t *g, uint32_t width, uint32_t height, unsigned int bit_depth)
{
	__real_rpivid_geom(g, width, height, bit_depth);
	if (!P.zc || (bit_depth != 8u)) {
		return;
	}
	P.w = width;
	P.h = height;
	P.h16 = (height + 15u) & ~15u;
	P.colh = P.h16 + P.h16 / 2u;
	g->luma_stride = P.colh * 128u;
	g->chroma_stride = P.colh * 128u;
	g->luma_size = (size_t)P.colh * 128u * g->cols;
	g->chroma_size = 4096u;        /* a marker: carved out of the luma BO below */
	P.luma_size = g->luma_size;
	P.chroma_marker = g->chroma_size;
	P.geom_set = 1;
	printf("ZC1 geom w=%u h=%u cols=%u colh=%u luma_off=0 chroma_off=%u stride=%u size=%zu\n", width, height, g->cols, P.colh,
		P.h16 * 128u, g->luma_stride, g->luma_size);
}


int __wrap_rpivid_dma_alloc_cached(rpivid_dma_t *d, size_t size)
{
	int k, rc = 0;

	pthread_mutex_lock(&P.lock);
	if (P.zc && P.geom_set && (size == P.luma_size)) {
		k = bo_new(size);
		if (k < 0) {
			P.nfail++;
			memset(d, 0, sizeof(*d));
			rc = -ENOMEM;
		}
		else {
			d->cpu = P.bo[k].cpu;
			d->pa = P.bo[k].pa;
			d->size = P.bo[k].size;
		}
	}
	else if (P.zc && P.geom_set && (size == P.chroma_marker)) {
		/* the chroma of RPIVIDBuf {y, c, mv} (rpivid_hevc.c): its luma is d - 1, a BO base;
		 * never a real 4 KiB allocation, which the block would overrun */
		const rpivid_dma_t *yd = d - 1;

		rc = -ENOMEM;
		for (k = 0; k < MAX_BOS; k++) {
			const bo_t *b = &P.bo[k];
			size_t off = (size_t)P.h16 * 128u;

			if (b->used && ((uint8_t *)yd->cpu == b->cpu) && (yd->pa == b->pa)) {
				d->cpu = b->cpu + off;
				d->pa = b->pa + off;
				d->size = b->size - off;
				rc = 0;
				break;
			}
		}
		if (rc != 0) {
			memset(d, 0, sizeof(*d));
			P.nfail++;
		}
	}
	else {
		pthread_mutex_unlock(&P.lock);
		return __real_rpivid_dma_alloc_cached(d, size);
	}
	pthread_mutex_unlock(&P.lock);
	return rc;
}


void __wrap_rpivid_dma_free(rpivid_dma_t *d)
{
	int k;

	for (k = 0; k < MAX_BOS; k++) {
		const bo_t *b = &P.bo[k];

		if (b->used && ((uint8_t *)d->cpu >= b->cpu) && ((uint8_t *)d->cpu < b->cpu + b->size)) {
			/* the BOs live to the end of the probe */
			memset(d, 0, sizeof(*d));
			return;
		}
	}
	__real_rpivid_dma_free(d);
}


/* ---- pictures ---- */

typedef struct {
	uint32_t w, h;
	uint8_t *y, *u, *v;            /* planar, strides w and w / 2 */
} planar_t;


static int planar_alloc(planar_t *p, uint32_t w, uint32_t h)
{
	p->w = w;
	p->h = h;
	p->y = malloc((size_t)w * h);
	p->u = malloc((size_t)(w / 2u) * (h / 2u));
	p->v = malloc((size_t)(w / 2u) * (h / 2u));
	return ((p->y != NULL) && (p->u != NULL) && (p->v != NULL)) ? 0 : -ENOMEM;
}


static void detile(planar_t *p, const bo_t *b)
{
	rpivid_sand8_to_planar(p->y, p->w, p->u, p->w / 2u, p->v, p->w / 2u, b->cpu, b->cpu + (size_t)P.h16 * 128u, P.colh * 128u,
		P.colh * 128u, p->w, p->h);
}


/* the inverse of detile: a planar picture into the BO's SAND layout */
static void tile(bo_t *b, const planar_t *p)
{
	uint32_t x, y;
	size_t cs = (size_t)P.colh * 128u, coff = (size_t)P.h16 * 128u;

	for (y = 0; y < p->h; y++) {
		for (x = 0; x < p->w; x++) {
			b->cpu[(x / 128u) * cs + (size_t)y * 128u + (x % 128u)] = p->y[(size_t)y * p->w + x];
		}
	}
	for (y = 0; y < p->h / 2u; y++) {
		for (x = 0; x < p->w / 2u; x++) {
			size_t a = coff + (size_t)((2u * x) / 128u) * cs + (size_t)y * 128u + ((2u * x) % 128u);

			b->cpu[a] = p->u[(size_t)y * (p->w / 2u) + x];
			b->cpu[a + 1u] = p->v[(size_t)y * (p->w / 2u) + x];
		}
	}
}


static void pattern(planar_t *p)
{
	uint32_t x, y;

	for (y = 0; y < p->h; y++) {
		for (x = 0; x < p->w; x++) {
			p->y[(size_t)y * p->w + x] = (uint8_t)(16u + ((x * 7u + y * 3u + ((x / 128u) * 37u)) % 220u));
		}
	}
	for (y = 0; y < p->h / 2u; y++) {
		for (x = 0; x < p->w / 2u; x++) {
			p->u[(size_t)y * (p->w / 2u) + x] = (uint8_t)(16u + ((x * 5u + y) % 225u));
			p->v[(size_t)y * (p->w / 2u) + x] = (uint8_t)(16u + ((x + y * 9u) % 225u));
		}
	}
}


/* max |a - b| over the frame's planes, -1 on a size mismatch */
static int frame_diff(const planar_t *p, const AVFrame *f)
{
	uint32_t x, y;
	int m = 0, d;

	if ((f->width != (int)p->w) || (f->height != (int)p->h)) {
		return -1;
	}
	for (y = 0; y < p->h; y++) {
		for (x = 0; x < p->w; x++) {
			d = abs((int)p->y[(size_t)y * p->w + x] - (int)f->data[0][(size_t)y * f->linesize[0] + x]);
			m = (d > m) ? d : m;
		}
	}
	for (y = 0; y < p->h / 2u; y++) {
		for (x = 0; x < p->w / 2u; x++) {
			d = abs((int)p->u[(size_t)y * (p->w / 2u) + x] - (int)f->data[1][(size_t)y * f->linesize[1] + x]);
			m = (d > m) ? d : m;
			d = abs((int)p->v[(size_t)y * (p->w / 2u) + x] - (int)f->data[2][(size_t)y * f->linesize[2] + x]);
			m = (d > m) ? d : m;
		}
	}
	return m;
}


/* FFmpeg's rpivid lines (attach, CPU fallback, statistics) on stdout, tagged */
static void ff_log(void *avcl, int level, const char *fmt, va_list vl)
{
	char line[512];

	(void)avcl;
	if (level > AV_LOG_INFO) {
		return;
	}
	vsnprintf(line, sizeof(line), fmt, vl);
	if ((strstr(line, "rpivid") != NULL) || (level <= AV_LOG_ERROR)) {
		printf("ZC1 ffmpeg %s%s", line, (line[0] != '\0' && line[strlen(line) - 1] == '\n') ? "" : "\n");
	}
}


/* Decode up to picture n with hevc_rpivid; *out = that picture (yuv420p). 0 or -1. */
static int decode(const char *path, int n, AVFrame *out, int *got_index)
{
	AVFormatContext *fmt = NULL;
	AVCodecContext *ctx = NULL;
	const AVCodec *codec;
	AVPacket *pkt = av_packet_alloc();
	AVFrame *f = av_frame_alloc();
	int vs, rc = -1, idx = 0, ret, eof = 0;

	if ((pkt == NULL) || (f == NULL)) {
		goto out;
	}
	av_log_set_callback(ff_log);
	if (avformat_open_input(&fmt, path, NULL, NULL) < 0) {
		printf("ZC1 decode error=open file=%s\n", path);
		goto out;
	}
	if (avformat_find_stream_info(fmt, NULL) < 0) {
		goto out;
	}
	vs = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
	codec = avcodec_find_decoder_by_name("hevc_rpivid");
	if ((vs < 0) || (codec == NULL) || (fmt->streams[vs]->codecpar->codec_id != AV_CODEC_ID_HEVC)) {
		printf("ZC1 decode error=no-hevc-stream-or-decoder\n");
		goto out;
	}
	ctx = avcodec_alloc_context3(codec);
	if ((ctx == NULL) || (avcodec_parameters_to_context(ctx, fmt->streams[vs]->codecpar) < 0)) {
		goto out;
	}
	ctx->thread_count = 1;
	/* only this decoder's pool goes into BOs: not find_stream_info's probe decoders */
	pthread_mutex_lock(&P.lock);
	P.zc = 1;
	pthread_mutex_unlock(&P.lock);
	if (avcodec_open2(ctx, codec, NULL) < 0) {
		printf("ZC1 decode error=avcodec_open2\n");
		goto out;
	}
	while (!eof) {
		ret = av_read_frame(fmt, pkt);
		if (ret < 0) {
			eof = 1;
			avcodec_send_packet(ctx, NULL);
		}
		else if (pkt->stream_index != vs) {
			av_packet_unref(pkt);
			continue;
		}
		else {
			avcodec_send_packet(ctx, pkt);
			av_packet_unref(pkt);
		}
		while (avcodec_receive_frame(ctx, f) == 0) {
			if (idx == n) {
				av_frame_move_ref(out, f);
				*got_index = idx;
				rc = 0;
				goto out;
			}
			idx++;
			av_frame_unref(f);
		}
	}
	printf("ZC1 decode error=only-%d-pictures\n", idx);

out:
	/* the decoder stays open: its pool BOs hold the pictures (the BOs are never freed) */
	av_frame_free(&f);
	av_packet_free(&pkt);
	return rc;
}


/* ---- GL ---- */

static int has_ext(const char *list, const char *name)
{
	size_t n = strlen(name);
	const char *p = list;

	while ((p != NULL) && ((p = strstr(p, name)) != NULL)) {
		if (((p == list) || (p[-1] == ' ')) && ((p[n] == ' ') || (p[n] == '\0'))) {
			return 1;
		}
		p += n;
	}
	return 0;
}

static PFNEGLQUERYDMABUFMODIFIERSEXTPROC queryModifiers;
static PFNEGLCREATEIMAGEKHRPROC createImage;
static PFNEGLDESTROYIMAGEKHRPROC destroyImage;
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC imageTargetTexture;
static GLuint prog_ext, prog_2d, fbo, fbo_tex;
static uint32_t fbo_w, fbo_h;


static GLuint shader(GLenum type, const char *src)
{
	GLuint s = glCreateShader(type);
	GLint ok = 0;
	char log[512];

	glShaderSource(s, 1, &src, NULL);
	glCompileShader(s);
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		glGetShaderInfoLog(s, sizeof(log), NULL, log);
		printf("ZC1 gl shader-error %s\n", log);
	}
	return s;
}


static GLuint program(const char *fs)
{
	static const char *vs =
		"attribute vec2 pos;\n"
		"varying vec2 uv;\n"
		"void main() { uv = pos * 0.5 + 0.5; gl_Position = vec4(pos, 0.0, 1.0); }\n";
	GLuint p = glCreateProgram();

	glAttachShader(p, shader(GL_VERTEX_SHADER, vs));
	glAttachShader(p, shader(GL_FRAGMENT_SHADER, fs));
	glBindAttribLocation(p, 0, "pos");
	glLinkProgram(p);
	return p;
}


static int gl_init(void)
{
	static const char *fs_ext =
		"#extension GL_OES_EGL_image_external : require\n"
		"precision highp float;\n"
		"uniform samplerExternalOES t;\n"
		"varying vec2 uv;\n"
		"void main() { gl_FragColor = texture2D(t, uv); }\n";
	static const char *fs_2d =
		"precision highp float;\n"
		"uniform sampler2D t;\n"
		"varying vec2 uv;\n"
		"void main() { gl_FragColor = texture2D(t, uv); }\n";
	static const GLfloat quad[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
	static GLuint vbo;

	prog_ext = program(fs_ext);
	prog_2d = program(fs_2d);
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
	glEnableVertexAttribArray(0);
	glGenFramebuffers(1, &fbo);
	glGenTextures(1, &fbo_tex);
	return (glGetError() == GL_NO_ERROR) ? 0 : -1;
}


static int fbo_size(uint32_t w, uint32_t h)
{
	if ((w == fbo_w) && (h == fbo_h)) {
		return 0;
	}
	glBindTexture(GL_TEXTURE_2D, fbo_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)w, (GLsizei)h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbo_tex, 0);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		printf("ZC1 gl error=fbo-incomplete\n");
		return -1;
	}
	fbo_w = w;
	fbo_h = h;
	glViewport(0, 0, (GLsizei)w, (GLsizei)h);
	return 0;
}


static void draw(GLuint prog, GLenum target, GLuint tex)
{
	glUseProgram(prog);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(target, tex);
	glUniform1i(glGetUniformLocation(prog, "t"), 0);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}


static GLuint tex_from_image(EGLImageKHR img)
{
	GLuint t;

	glGenTextures(1, &t);
	glBindTexture(GL_TEXTURE_EXTERNAL_OES, t);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	imageTargetTexture(GL_TEXTURE_EXTERNAL_OES, (GLeglImageOES)img);
	return t;
}


typedef struct {
	uint32_t fourcc;
	int nplanes;
	int fd[3];
	uint32_t offset[3], pitch[3];
	uint64_t modifier;
	uint32_t w, h;
} imp_t;


static EGLImageKHR import(EGLDisplay dpy, const imp_t *m)
{
	EGLint a[64];
	int n = 0, i;
	static const EGLint fdk[3] = { EGL_DMA_BUF_PLANE0_FD_EXT, EGL_DMA_BUF_PLANE1_FD_EXT, EGL_DMA_BUF_PLANE2_FD_EXT };
	static const EGLint ofk[3] = { EGL_DMA_BUF_PLANE0_OFFSET_EXT, EGL_DMA_BUF_PLANE1_OFFSET_EXT, EGL_DMA_BUF_PLANE2_OFFSET_EXT };
	static const EGLint pik[3] = { EGL_DMA_BUF_PLANE0_PITCH_EXT, EGL_DMA_BUF_PLANE1_PITCH_EXT, EGL_DMA_BUF_PLANE2_PITCH_EXT };
	static const EGLint lok[3] = { EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT, EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT,
		EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT };
	static const EGLint hik[3] = { EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT, EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT,
		EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT };

	a[n++] = EGL_WIDTH;
	a[n++] = (EGLint)m->w;
	a[n++] = EGL_HEIGHT;
	a[n++] = (EGLint)m->h;
	a[n++] = EGL_LINUX_DRM_FOURCC_EXT;
	a[n++] = (EGLint)m->fourcc;
	for (i = 0; i < m->nplanes; i++) {
		a[n++] = fdk[i];
		a[n++] = m->fd[i];
		a[n++] = ofk[i];
		a[n++] = (EGLint)m->offset[i];
		a[n++] = pik[i];
		a[n++] = (EGLint)m->pitch[i];
		a[n++] = lok[i];
		a[n++] = (EGLint)(uint32_t)(m->modifier & 0xffffffffu);
		a[n++] = hik[i];
		a[n++] = (EGLint)(uint32_t)(m->modifier >> 32);
	}
	if ((m->fourcc == DRM_FORMAT_NV12) || (m->fourcc == DRM_FORMAT_YUV420)) {
		a[n++] = EGL_YUV_COLOR_SPACE_HINT_EXT;
		a[n++] = EGL_ITU_REC709_EXT;
		a[n++] = EGL_SAMPLE_RANGE_HINT_EXT;
		a[n++] = EGL_YUV_NARROW_RANGE_EXT;
	}
	a[n++] = EGL_NONE;
	return createImage(dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, (EGLClientBuffer)NULL, a);
}


/* draw the image into a w x h FBO, read RGBA back */
static int render(EGLDisplay dpy, const imp_t *m, uint32_t w, uint32_t h, uint8_t *rgba, EGLint *eglerr)
{
	EGLImageKHR img = import(dpy, m);
	GLuint t;
	GLenum ge;

	*eglerr = eglGetError();
	if (img == EGL_NO_IMAGE_KHR) {
		return -1;
	}
	if (fbo_size(w, h) < 0) {
		return -1;
	}
	t = tex_from_image(img);
	draw(prog_ext, GL_TEXTURE_EXTERNAL_OES, t);
	glReadPixels(0, 0, (GLsizei)w, (GLsizei)h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	ge = glGetError();
	glDeleteTextures(1, &t);
	destroyImage(dpy, img);
	if (ge != GL_NO_ERROR) {
		printf("ZC1 gl error=0x%x\n", ge);
		return -1;
	}
	return 0;
}


static int clamp8(double v)
{
	int i = (int)(v + 0.5);

	return (i < 0) ? 0 : ((i > 255) ? 255 : i);
}


/* max channel difference of rgba against the CPU narrow-range conversion with (kr, kb) */
static int rgb_diff(const uint8_t *rgba, const planar_t *p, double kr, double kb, uint64_t *over4)
{
	double kg = 1.0 - kr - kb, ys = 255.0 / 219.0, cs = 255.0 / 224.0;
	uint32_t x, y;
	int m = 0, c, d, ref[3];

	*over4 = 0;
	for (y = 0; y < p->h; y++) {
		for (x = 0; x < p->w; x++) {
			double Y = ((double)p->y[(size_t)y * p->w + x] - 16.0) * ys;
			double U = ((double)p->u[(size_t)(y / 2u) * (p->w / 2u) + x / 2u] - 128.0) * cs;
			double V = ((double)p->v[(size_t)(y / 2u) * (p->w / 2u) + x / 2u] - 128.0) * cs;
			const uint8_t *o = rgba + ((size_t)y * p->w + x) * 4u;

			ref[0] = clamp8(Y + 2.0 * (1.0 - kr) * V);
			ref[1] = clamp8(Y - 2.0 * kb * (1.0 - kb) / kg * U - 2.0 * kr * (1.0 - kr) / kg * V);
			ref[2] = clamp8(Y + 2.0 * (1.0 - kb) * U);
			for (c = 0; c < 3; c++) {
				d = abs((int)o[c] - ref[c]);
				m = (d > m) ? d : m;
				*over4 += (d > 4) ? 1u : 0u;
			}
		}
	}
	return m;
}


static double time_draws(GLuint prog, GLenum target, GLuint tex, int iters)
{
	double t0;
	int i;

	draw(prog, target, tex);
	glFinish();
	t0 = now_ms();
	for (i = 0; i < iters; i++) {
		draw(prog, target, tex);
		glFlush();
	}
	glFinish();
	return (now_ms() - t0) / iters;
}


int main(int argc, char **argv)
{
	const char *file = NULL;
	int n = 5, iters = 100, synthetic = 0, i, k, src = -1, got = -1, fd, fails = 0;
	planar_t ref = { 0 }, tmp = { 0 };
	AVFrame *frame = av_frame_alloc();
	EGLDisplay dpy;
	EGLContext ctx;
	EGLint major, minor, eglerr;
	const char *ext;
	uint8_t *rgba;
	uint32_t w, h, x, y;
	double t0, t_detile = -1.0;

	for (i = 1; i < argc; i++) {
		if ((strcmp(argv[i], "-n") == 0) && (i + 1 < argc)) {
			n = atoi(argv[++i]);
		}
		else if ((strcmp(argv[i], "-i") == 0) && (i + 1 < argc)) {
			iters = atoi(argv[++i]);
		}
		else if (strcmp(argv[i], "-synthetic") == 0) {
			synthetic = 1;
		}
		else {
			file = argv[i];
		}
	}
	if (file == NULL) {
		synthetic = 1;
	}
	if (iters < 1) {
		iters = 1;
	}
	printf("ZC1 start file=%s picture=%d iterations=%d mode=%s\n", (file != NULL) ? file : "-", n, iters, synthetic ? "synthetic" : "decode");

	P.rfd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
	if (P.rfd < 0) {
		printf("ZC1 result=FAIL why=open-renderD128 errno=%d\n", errno);
		return 1;
	}

	/* 1-2: a picture in a BO, and its CPU de-tile */
	if (!synthetic) {
		if ((decode(file, n, frame, &got) == 0) && (frame->format == AV_PIX_FMT_YUV420P) && P.geom_set) {
			if (planar_alloc(&ref, P.w, P.h) < 0 || planar_alloc(&tmp, P.w, P.h) < 0) {
				return 1;
			}
			for (k = 0; k < MAX_BOS; k++) {
				if (!P.bo[k].used) {
					continue;
				}
				t0 = now_ms();
				detile(&tmp, &P.bo[k]);
				t_detile = now_ms() - t0;
				if (frame_diff(&tmp, frame) == 0) {
					src = k;
					memcpy(ref.y, tmp.y, (size_t)P.w * P.h);
					memcpy(ref.u, tmp.u, (size_t)(P.w / 2u) * (P.h / 2u));
					memcpy(ref.v, tmp.v, (size_t)(P.w / 2u) * (P.h / 2u));
					break;
				}
			}
			printf("ZC1 decode result=%s picture=%d format=%s %dx%d bos=%d contiguous=%d alloc_fail=%d pa_check=%s match_bo=%d pa=0x%llx..0x%llx detile_cpu_uncached_ms=%.2f\n",
				(src >= 0) ? "PASS" : "FAIL", got, av_get_pix_fmt_name(frame->format), frame->width, frame->height, P.nbo, P.ncontig,
				P.nfail, (P.npa_fail == 0) ? "PASS" : "FAIL", src, (unsigned long long)P.pa_min, (unsigned long long)P.pa_max, t_detile);
			fails += (src < 0) || (P.ncontig != P.nbo);
		}
		else {
			printf("ZC1 decode result=FAIL format=%s geom=%d bos=%d alloc_fail=%d pa_check=%s (falling back to -synthetic)\n",
				(frame->format >= 0) ? av_get_pix_fmt_name(frame->format) : "none", P.geom_set, P.nbo, P.nfail,
				(P.npa_fail == 0) ? "PASS" : "FAIL");
			fails++;
		}
		P.zc = 0;
	}
	if (src < 0) {
		/* synthetic: the pattern tiled into a BO of the 1080p geometry */
		P.w = 1920u;
		P.h = 1080u;
		P.h16 = 1088u;
		P.colh = P.h16 + P.h16 / 2u;
		if (planar_alloc(&ref, P.w, P.h) < 0 || planar_alloc(&tmp, P.w, P.h) < 0) {
			return 1;
		}
		src = bo_new((size_t)P.colh * 128u * ((P.w + 127u) / 128u));
		if (src < 0) {
			printf("ZC1 result=FAIL why=create-bo rc=%d\n", src);
			return 1;
		}
		pattern(&ref);
		tile(&P.bo[src], &ref);
		t0 = now_ms();
		detile(&tmp, &P.bo[src]);
		t_detile = now_ms() - t0;
		k = (memcmp(tmp.y, ref.y, (size_t)P.w * P.h) == 0) && (memcmp(tmp.u, ref.u, (size_t)(P.w / 2u) * (P.h / 2u)) == 0) &&
			(memcmp(tmp.v, ref.v, (size_t)(P.w / 2u) * (P.h / 2u)) == 0);
		printf("ZC1 synthetic tile_detile=%s contiguous=%d pa_check=PASS pa=0x%llx detile_cpu_uncached_ms=%.2f\n", k ? "PASS" : "FAIL",
			P.bo[src].contiguous, (unsigned long long)P.bo[src].pa, t_detile);
		fails += !k;
	}
	w = P.w;
	h = P.h;
	{
		uint64_t sum = 0, sq = 0;

		for (i = 0; i < (int)(w * h); i++) {
			sum += ref.y[i];
			sq += (uint64_t)ref.y[i] * ref.y[i];
		}
		printf("ZC1 picture luma_mean=%.1f luma_var=%.1f (a flat picture proves little)\n", (double)sum / (w * h),
			(double)sq / (w * h) - ((double)sum / (w * h)) * ((double)sum / (w * h)));
	}

	/* 3: EGL */
	dpy = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
	if ((dpy == EGL_NO_DISPLAY) || !eglInitialize(dpy, &major, &minor)) {
		printf("ZC1 result=FAIL why=egl-init err=0x%x\n", eglGetError());
		return 1;
	}
	ext = eglQueryString(dpy, EGL_EXTENSIONS);
	printf("ZC1 egl version=%d.%d vendor=%s dma_buf_import=%d modifiers=%d surfaceless_context=%d no_config=%d\n", major, minor,
		eglQueryString(dpy, EGL_VENDOR), has_ext(ext, "EGL_EXT_image_dma_buf_import"),
		has_ext(ext, "EGL_EXT_image_dma_buf_import_modifiers"), has_ext(ext, "EGL_KHR_surfaceless_context"),
		has_ext(ext, "EGL_KHR_no_config_context"));
	eglBindAPI(EGL_OPENGL_ES_API);
	{
		static const EGLint ca[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };

		ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ca);
		if ((ctx == EGL_NO_CONTEXT) || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
			printf("ZC1 result=FAIL why=egl-context err=0x%x\n", eglGetError());
			return 1;
		}
	}
	printf("ZC1 gl renderer=\"%s\" version=\"%s\" egl_image_external=%d\n", (const char *)glGetString(GL_RENDERER),
		(const char *)glGetString(GL_VERSION), has_ext((const char *)glGetString(GL_EXTENSIONS), "GL_OES_EGL_image_external"));
	queryModifiers = (PFNEGLQUERYDMABUFMODIFIERSEXTPROC)eglGetProcAddress("eglQueryDmaBufModifiersEXT");
	createImage = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
	destroyImage = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
	imageTargetTexture = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
	if ((createImage == NULL) || (destroyImage == NULL) || (imageTargetTexture == NULL)) {
		printf("ZC1 result=FAIL why=no-image-entry-points\n");
		return 1;
	}
	if (queryModifiers != NULL) {
		static const uint32_t fmts[] = { DRM_FORMAT_NV12, DRM_FORMAT_R8, DRM_FORMAT_GR88, DRM_FORMAT_YUV420 };
		static const char *names[] = { "NV12", "R8", "GR88", "YUV420" };
		EGLuint64KHR mods[16];
		EGLBoolean ext_only[16];
		EGLint nm = 0;
		int j;

		for (i = 0; i < 4; i++) {
			int sand = -1, lin = -1;

			nm = 0;
			queryModifiers(dpy, (EGLint)fmts[i], 16, mods, ext_only, &nm);
			for (j = 0; j < nm; j++) {
				if (mods[j] == DRM_FORMAT_MOD_BROADCOM_SAND128) {
					sand = ext_only[j];
				}
				if (mods[j] == DRM_FORMAT_MOD_LINEAR) {
					lin = ext_only[j];
				}
			}
			printf("ZC1 modifiers format=%s n=%d sand128=%s linear=%s\n", names[i], nm,
				(sand < 0) ? "no" : (sand ? "external-only" : "yes"), (lin < 0) ? "no" : (lin ? "external-only" : "yes"));
		}
	}
	if (gl_init() < 0) {
		printf("ZC1 result=FAIL why=gl-init\n");
		return 1;
	}

	fd = bo_dmabuf(&P.bo[src]);
	if (fd < 0) {
		printf("ZC1 result=FAIL why=prime-export rc=%d\n", fd);
		return 1;
	}
	rgba = malloc((size_t)w * h * 4u);
	if (rgba == NULL) {
		return 1;
	}

	/* yuv arm: the planes, exactly */
	{
		const uint64_t mod = DRM_FORMAT_MOD_BROADCOM_SAND128_COL_HEIGHT(P.colh);
		imp_t yi = { DRM_FORMAT_R8, 1, { fd, -1, -1 }, { 0, 0, 0 }, { w, 0, 0 }, mod, w, h };
		imp_t ci = { DRM_FORMAT_GR88, 1, { fd, -1, -1 }, { P.h16 * 128u, 0, 0 }, { w, 0, 0 }, mod, w / 2u, h / 2u };
		uint64_t bad = 0;
		int m = 0, d, rc;

		rc = render(dpy, &yi, w, h, rgba, &eglerr);
		if (rc == 0) {
			for (y = 0; y < h; y++) {
				for (x = 0; x < w; x++) {
					d = abs((int)rgba[((size_t)y * w + x) * 4u] - (int)ref.y[(size_t)y * w + x]);
					m = (d > m) ? d : m;
					bad += (d != 0) ? 1u : 0u;
				}
			}
		}
		printf("ZC1 yuv plane=Y import=%s egl_err=0x%x result=%s max_diff=%d bad=%llu\n", (rc == 0) ? "ok" : "fail", eglerr,
			((rc == 0) && (m == 0)) ? "PASS" : "FAIL", m, (unsigned long long)bad);
		fails += (rc != 0) || (m != 0);

		m = 0;
		bad = 0;
		rc = render(dpy, &ci, w / 2u, h / 2u, rgba, &eglerr);
		if (rc == 0) {
			for (y = 0; y < h / 2u; y++) {
				for (x = 0; x < w / 2u; x++) {
					const uint8_t *o = rgba + ((size_t)y * (w / 2u) + x) * 4u;

					d = abs((int)o[0] - (int)ref.u[(size_t)y * (w / 2u) + x]);
					d = (abs((int)o[1] - (int)ref.v[(size_t)y * (w / 2u) + x]) > d) ? abs((int)o[1] - (int)ref.v[(size_t)y * (w / 2u) + x]) : d;
					m = (d > m) ? d : m;
					bad += (d != 0) ? 1u : 0u;
				}
			}
		}
		printf("ZC1 yuv plane=CbCr import=%s egl_err=0x%x result=%s max_diff=%d bad=%llu\n", (rc == 0) ? "ok" : "fail", eglerr,
			((rc == 0) && (m == 0)) ? "PASS" : "FAIL", m, (unsigned long long)bad);
		fails += (rc != 0) || (m != 0);
	}

	/* nv12 arm and its import variants */
	{
		static const char *vname[] = { "colh-param.pitch-width", "colh-param.pitch-128", "bare-modifier.pitch-colh" };
		int v, best709 = 256, rc;

		for (v = 0; v < 3; v++) {
			imp_t ni = { DRM_FORMAT_NV12, 2, { fd, fd, -1 }, { 0, P.h16 * 128u, 0 }, { w, w, 0 },
				DRM_FORMAT_MOD_BROADCOM_SAND128_COL_HEIGHT(P.colh), w, h };
			uint64_t o709 = 0, o601 = 0;
			int m709 = -1, m601 = -1;

			if (v == 1) {
				ni.pitch[0] = ni.pitch[1] = 128u;
			}
			else if (v == 2) {
				ni.pitch[0] = ni.pitch[1] = P.colh;
				ni.modifier = DRM_FORMAT_MOD_BROADCOM_SAND128;
			}
			rc = render(dpy, &ni, w, h, rgba, &eglerr);
			if (rc == 0) {
				m709 = rgb_diff(rgba, &ref, 0.2126, 0.0722, &o709);
				m601 = rgb_diff(rgba, &ref, 0.299, 0.114, &o601);
			}
			printf("ZC1 nv12 variant=%s import=%s egl_err=0x%x max_diff_709=%d over4_709=%llu max_diff_601=%d over4_601=%llu\n",
				vname[v], (rc == 0) ? "ok" : "fail", eglerr, m709, (unsigned long long)o709, m601, (unsigned long long)o601);
			if ((v == 0) && (rc == 0)) {
				best709 = m709;
				printf("ZC1 nv12 result=%s matrix=%s (tolerance 4: the CPU reference is float, Mesa's lowering fp16/fp32)\n",
					(m709 <= 4) ? "PASS" : "FAIL", (m709 <= m601) ? "bt709" : "bt601");
			}
		}
		fails += (best709 > 4);
	}

	/* 4: timing */
	{
		imp_t ni = { DRM_FORMAT_NV12, 2, { fd, fd, -1 }, { 0, P.h16 * 128u, 0 }, { w, w, 0 },
			DRM_FORMAT_MOD_BROADCOM_SAND128_COL_HEIGHT(P.colh), w, h };
		EGLImageKHR img = import(dpy, &ni);
		GLuint tsand = 0, trgba, tpl[3];
		double t_sand = -1, t_rgba, t_up, t_lin = -1, t_wr = -1;
		int lk, lfd;

		if (img != EGL_NO_IMAGE_KHR) {
			tsand = tex_from_image(img);
			fbo_size(w, h);
			t_sand = time_draws(prog_ext, GL_TEXTURE_EXTERNAL_OES, tsand, iters);
		}

		/* an RGBA texture of the same size: the draw alone */
		glGenTextures(1, &trgba);
		glBindTexture(GL_TEXTURE_2D, trgba);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)w, (GLsizei)h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
		t_rgba = time_draws(prog_2d, GL_TEXTURE_2D, trgba, iters);

		/* today's browser path: three luminance planes uploaded per picture */
		glGenTextures(3, tpl);
		for (i = 0; i < 3; i++) {
			glBindTexture(GL_TEXTURE_2D, tpl[i]);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, (GLsizei)((i == 0) ? w : w / 2u), (GLsizei)((i == 0) ? h : h / 2u), 0,
				GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
		}
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glFinish();
		t0 = now_ms();
		for (k = 0; k < iters / 10 + 1; k++) {
			glBindTexture(GL_TEXTURE_2D, tpl[0]);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, (GLsizei)w, (GLsizei)h, GL_LUMINANCE, GL_UNSIGNED_BYTE, ref.y);
			glBindTexture(GL_TEXTURE_2D, tpl[1]);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, (GLsizei)(w / 2u), (GLsizei)(h / 2u), GL_LUMINANCE, GL_UNSIGNED_BYTE, ref.u);
			glBindTexture(GL_TEXTURE_2D, tpl[2]);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, (GLsizei)(w / 2u), (GLsizei)(h / 2u), GL_LUMINANCE, GL_UNSIGNED_BYTE, ref.v);
			draw(prog_2d, GL_TEXTURE_2D, tpl[0]);
			glFinish();
		}
		t_up = (now_ms() - t0) / (iters / 10 + 1);

		/* path A: a LINEAR YUV420 BO (the CPU write into uncached memory, then the import) */
		lk = bo_new((size_t)w * h * 3u / 2u);
		if (lk >= 0) {
			uint8_t *d = P.bo[lk].cpu;

			t0 = now_ms();
			memcpy(d, ref.y, (size_t)w * h);
			memcpy(d + (size_t)w * h, ref.u, (size_t)(w / 2u) * (h / 2u));
			memcpy(d + (size_t)w * h + (size_t)(w / 2u) * (h / 2u), ref.v, (size_t)(w / 2u) * (h / 2u));
			t_wr = now_ms() - t0;
			lfd = bo_dmabuf(&P.bo[lk]);
			if (lfd >= 0) {
				imp_t li = { DRM_FORMAT_YUV420, 3, { lfd, lfd, lfd },
					{ 0, w * h, w * h + (w / 2u) * (h / 2u) }, { w, w / 2u, w / 2u }, DRM_FORMAT_MOD_LINEAR, w, h };
				EGLImageKHR limg = import(dpy, &li);

				if (limg != EGL_NO_IMAGE_KHR) {
					GLuint tl = tex_from_image(limg);

					fbo_size(w, h);
					t_lin = time_draws(prog_ext, GL_TEXTURE_EXTERNAL_OES, tl, iters);
				}
				else {
					printf("ZC1 linear import=fail egl_err=0x%x\n", eglGetError());
				}
			}
		}
		printf("ZC1 time sand_import_draw_ms=%.3f rgba_draw_ms=%.3f sand_blit_est_ms=%.3f upload3_plus_draw_ms=%.3f "
			"linear_yuv420_draw_ms=%.3f uncached_write_planar_ms=%.2f detile_cpu_uncached_ms=%.2f draws=%d\n",
			t_sand, t_rgba, (t_sand >= 0) ? t_sand - t_rgba : -1.0, t_up, t_lin, t_wr, t_detile, iters);
		fails += (t_sand < 0);
	}

	printf("ZC1 result=%s fails=%d\n", (fails == 0) ? "PASS" : "FAIL", fails);
	return (fails == 0) ? 0 : 1;
}
