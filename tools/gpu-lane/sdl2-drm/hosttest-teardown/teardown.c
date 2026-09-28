/*
 * Host reproduction of the SDL 2.30.12 KMSDRM teardown-order use-after-free
 * (docs/misc/2026-09-28-stk-scaled-exit-fault.md).
 *
 * Drives the host's own Mesa GBM + EGL on a render node through exactly the
 * call sequence of SDL's KMSDRM backend: the swap bookkeeping of
 * KMSDRM_GLES_SwapWindow (with our patch 0009: swap, lock the new front,
 * release the old front, bo <- next_bo, next_bo <- new), then
 * KMSDRM_DestroySurfaces in one of two orders:
 *
 *   old    SDL 2.30.12: eglDestroySurface, then gbm_surface_release_buffer x2
 *   fixed  patch 0010 (upstream 9cc2f248f5): release x2, then eglDestroySurface
 *
 * Mesa's dri2_drm_destroy_surface() frees the dri2_egl_surface but leaves the
 * gbm surface's dri_private pointing at it, so in the old order
 * release_buffer() reads freed memory. Run under valgrind (run.sh).
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

#ifndef EGL_PLATFORM_GBM_KHR
#define EGL_PLATFORM_GBM_KHR 0x31D7
#endif

static int fail(const char *what)
{
	fprintf(stderr, "teardown: %s failed (egl 0x%x)\n", what, eglGetError());
	return 2;
}

int main(int argc, char **argv)
{
	const char *node = "/dev/dri/renderD128";
	int fixed, w, h, i;

	if (argc != 4 || (strcmp(argv[1], "old") != 0 && strcmp(argv[1], "fixed") != 0)) {
		fprintf(stderr, "usage: %s old|fixed W H\n", argv[0]);
		return 1;
	}
	fixed = (strcmp(argv[1], "fixed") == 0);
	w = atoi(argv[2]);
	h = atoi(argv[3]);

	int fd = open(node, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		perror(node);
		return 2;
	}

	struct gbm_device *gbm = gbm_create_device(fd);
	if (gbm == NULL)
		return fail("gbm_create_device");

	PFNEGLGETPLATFORMDISPLAYEXTPROC getPlatformDisplay =
		(PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
	if (getPlatformDisplay == NULL)
		return fail("eglGetPlatformDisplayEXT lookup");

	EGLDisplay dpy = getPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
	if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, NULL, NULL))
		return fail("eglInitialize");
	eglBindAPI(EGL_OPENGL_ES_API);

	/* SDL_EGL_ChooseConfig with SDL_EGL_SetRequiredVisualId(XRGB8888). */
	static const EGLint cattr[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE
	};
	EGLConfig cfgs[64], cfg = NULL;
	EGLint n = 0;
	eglChooseConfig(dpy, cattr, cfgs, 64, &n);
	for (i = 0; i < n; i++) {
		EGLint vid;
		if (eglGetConfigAttrib(dpy, cfgs[i], EGL_NATIVE_VISUAL_ID, &vid) && vid == GBM_FORMAT_XRGB8888) {
			cfg = cfgs[i];
			break;
		}
	}
	if (cfg == NULL)
		return fail("XRGB8888 EGLConfig");

	static const EGLint ctxattr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxattr);
	if (ctx == EGL_NO_CONTEXT)
		return fail("eglCreateContext");

	/* KMSDRM_CreateSurfaces: SCANOUT|RENDERING; a render node may refuse SCANOUT. */
	struct gbm_surface *gs = gbm_surface_create(gbm, w, h, GBM_FORMAT_XRGB8888,
		GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
	if (gs == NULL)
		gs = gbm_surface_create(gbm, w, h, GBM_FORMAT_XRGB8888, GBM_BO_USE_RENDERING);
	if (gs == NULL)
		return fail("gbm_surface_create");

	EGLSurface es = eglCreateWindowSurface(dpy, cfg, (EGLNativeWindowType)gs, NULL);
	if (es == EGL_NO_SURFACE)
		return fail("eglCreateWindowSurface");
	if (!eglMakeCurrent(dpy, es, es, ctx))
		return fail("eglMakeCurrent");

	/* KMSDRM_GLES_SwapWindow bookkeeping (patch 0009 order), 4 frames: after
	 * the second one both bo and next_bo hold a locked buffer, as at any exit. */
	struct gbm_bo *bo = NULL, *next_bo = NULL;
	for (i = 0; i < 4; i++) {
		glClearColor(i & 1, 0.5f, 0.25f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		if (!eglSwapBuffers(dpy, es))
			return fail("eglSwapBuffers");
		struct gbm_bo *new_bo = gbm_surface_lock_front_buffer(gs);
		if (new_bo == NULL)
			return fail("gbm_surface_lock_front_buffer");
		if (bo != NULL)
			gbm_surface_release_buffer(gs, bo);
		bo = next_bo;
		next_bo = new_bo;
	}

	printf("teardown: order=%s %dx%d bo=%p next_bo=%p\n", fixed ? "fixed" : "old", w, h,
		(void *)bo, (void *)next_bo);

	/* KMSDRM_DestroySurfaces. */
	if (fixed) {
		gbm_surface_release_buffer(gs, bo);
		gbm_surface_release_buffer(gs, next_bo);
		eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		eglDestroySurface(dpy, es);
	}
	else {
		eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		eglDestroySurface(dpy, es);
		gbm_surface_release_buffer(gs, bo);
		gbm_surface_release_buffer(gs, next_bo);
	}
	gbm_surface_destroy(gs);

	eglDestroyContext(dpy, ctx);
	eglTerminate(dpy);
	gbm_device_destroy(gbm);
	close(fd);
	printf("teardown: done\n");
	return 0;
}
