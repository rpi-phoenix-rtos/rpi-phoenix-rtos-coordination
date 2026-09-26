/* E7 link probe: the call surface of a kmscube-class GBM+EGL+GLES2 program. */
#include <stdio.h>
#include <fcntl.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
int main(void)
{
	int fd = open("/dev/dri/card0", O_RDWR);
	drmModeRes *res = drmModeGetResources(fd);
	drmModeConnector *conn = drmModeGetConnector(fd, res->connectors[0]);
	struct gbm_device *gbm = gbm_create_device(fd);
	struct gbm_surface *gs = gbm_surface_create(gbm, 1920, 1080, GBM_FORMAT_XRGB8888,
	                                             GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
	PFNEGLGETPLATFORMDISPLAYEXTPROC gpd = (void *)eglGetProcAddress("eglGetPlatformDisplayEXT");
	EGLDisplay dpy = gpd ? gpd(EGL_PLATFORM_GBM_KHR, gbm, NULL) : eglGetDisplay((EGLNativeDisplayType)gbm);
	EGLint maj, min, n; EGLConfig cfg;
	eglInitialize(dpy, &maj, &min);
	eglBindAPI(EGL_OPENGL_ES_API);
	static const EGLint ca[] = { EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
	eglChooseConfig(dpy, ca, &cfg, 1, &n);
	static const EGLint cx[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, cx);
	EGLSurface surf = eglCreateWindowSurface(dpy, cfg, (EGLNativeWindowType)gs, NULL);
	eglMakeCurrent(dpy, surf, surf, ctx);
	glClearColor(0.2f, 0.3f, 0.4f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	GLuint sh = glCreateShader(GL_VERTEX_SHADER);
	glCompileShader(sh);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	eglSwapBuffers(dpy, surf);
	struct gbm_bo *bo = gbm_surface_lock_front_buffer(gs);
	uint32_t h[4] = { gbm_bo_get_handle(bo).u32 }, p[4] = { gbm_bo_get_stride(bo) }, o[4] = { 0 }, fb;
	drmModeAddFB2(fd, 1920, 1080, GBM_FORMAT_XRGB8888, h, p, o, &fb, 0);
	drmModeSetCrtc(fd, res->crtcs[0], fb, 0, 0, &conn->connector_id, 1, &conn->modes[0]);
	drmModePageFlip(fd, res->crtcs[0], fb, DRM_MODE_PAGE_FLIP_EVENT, NULL);
	drmEventContext ev = { .version = 2 };
	drmHandleEvent(fd, &ev);
	gbm_surface_release_buffer(gs, bo);
	printf("ok %d.%d\n", maj, min);
	return 0;
}
