/*
 * pl_phoenix_vid.c — Phoenix video shim for Quakespasm: replaces gl_vidsdl.c.
 *
 * Owns the `vid` global, the GL extension function-pointer globals + capability
 * flags that gl_vidsdl.c used to define, and the VID_ + GL_ rendering entry points.
 *
 * The GL context + FBO are created in pl_phoenix_glctx.c (Mesa-header-only, to keep
 * Mesa's pipe/st headers away from Quake's headers). Rendering goes to an offscreen
 * RGBA8+DEPTH24 FBO at native 1024x768; GL_EndRendering presents it to /dev/fb0
 * (the proven rpi4-glcube path; fullscreen RT works since the MMU PT grew to 128 MiB).
 *
 * First-light forces the BASIC fixed-function path: all GL extension pointers stay
 * NULL and every gl_*_able capability is false, so Quake never takes the
 * multitexture / VBO / GLSL paths (those pointers are never dereferenced). Enabling
 * them (assigning the real libGL-phoenix entry points) is a later optimization step.
 */
#include "quakedef.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <phoenix/fbcon.h>

/* GL context/FBO helpers (pl_phoenix_glctx.c) — plain C API, no Quake/Mesa types. */
int  qsv3d_init(int w, int h);
void qsv3d_make_current(void);

#define VID_W 1024
#define VID_H 768

viddef_t vid;                   /* global video state (declared in vid.h) */

/* --- GL extension function pointers (NULL = basic path; see header note) --- */
PFNGLMULTITEXCOORD2FARBPROC      GL_MTexCoord2fFunc = NULL;
PFNGLACTIVETEXTUREARBPROC        GL_SelectTextureFunc = NULL;
PFNGLCLIENTACTIVETEXTUREARBPROC  GL_ClientActiveTextureFunc = NULL;
PFNGLBINDBUFFERARBPROC           GL_BindBufferFunc = NULL;
PFNGLBUFFERDATAARBPROC           GL_BufferDataFunc = NULL;
PFNGLBUFFERSUBDATAARBPROC        GL_BufferSubDataFunc = NULL;
PFNGLDELETEBUFFERSARBPROC        GL_DeleteBuffersFunc = NULL;
PFNGLGENBUFFERSARBPROC           GL_GenBuffersFunc = NULL;
QS_PFNGLCREATESHADERPROC         GL_CreateShaderFunc = NULL;
QS_PFNGLDELETESHADERPROC         GL_DeleteShaderFunc = NULL;
QS_PFNGLDELETEPROGRAMPROC        GL_DeleteProgramFunc = NULL;
QS_PFNGLSHADERSOURCEPROC         GL_ShaderSourceFunc = NULL;
QS_PFNGLCOMPILESHADERPROC        GL_CompileShaderFunc = NULL;
QS_PFNGLGETSHADERIVPROC          GL_GetShaderivFunc = NULL;
QS_PFNGLGETSHADERINFOLOGPROC     GL_GetShaderInfoLogFunc = NULL;
QS_PFNGLGETPROGRAMIVPROC         GL_GetProgramivFunc = NULL;
QS_PFNGLGETPROGRAMINFOLOGPROC    GL_GetProgramInfoLogFunc = NULL;
QS_PFNGLCREATEPROGRAMPROC        GL_CreateProgramFunc = NULL;
QS_PFNGLATTACHSHADERPROC         GL_AttachShaderFunc = NULL;
QS_PFNGLLINKPROGRAMPROC          GL_LinkProgramFunc = NULL;
QS_PFNGLBINDATTRIBLOCATIONFUNC   GL_BindAttribLocationFunc = NULL;
QS_PFNGLUSEPROGRAMPROC           GL_UseProgramFunc = NULL;
QS_PFNGLGETATTRIBLOCATIONPROC    GL_GetAttribLocationFunc = NULL;
QS_PFNGLVERTEXATTRIBPOINTERPROC  GL_VertexAttribPointerFunc = NULL;
QS_PFNGLENABLEVERTEXATTRIBARRAYPROC  GL_EnableVertexAttribArrayFunc = NULL;
QS_PFNGLDISABLEVERTEXATTRIBARRAYPROC GL_DisableVertexAttribArrayFunc = NULL;
QS_PFNGLGETUNIFORMLOCATIONPROC   GL_GetUniformLocationFunc = NULL;
QS_PFNGLUNIFORM1IPROC            GL_Uniform1iFunc = NULL;
QS_PFNGLUNIFORM1FPROC            GL_Uniform1fFunc = NULL;
QS_PFNGLUNIFORM3FPROC            GL_Uniform3fFunc = NULL;
QS_PFNGLUNIFORM4FPROC            GL_Uniform4fFunc = NULL;

/* --- GL capability flags (all false => basic fixed-function path) --- */
qboolean gl_mtexable = false;
qboolean gl_anisotropy_able = false;
qboolean gl_glsl_able = false;
qboolean gl_glsl_gamma_able = false;
qboolean gl_glsl_alias_able = false;
qboolean gl_packed_pixels = false;
qboolean gl_texture_NPOT = false;
qboolean gl_texture_env_combine = false;
qboolean gl_texture_env_add = false;
qboolean gl_vbo_able = false;
float gl_max_anisotropy = 1.0f;

/* --- more gl_vidsdl.c-owned globals --- */
QS_PFNGENERATEMIPMAP GL_GenerateMipmap = NULL;
GLint gl_max_texture_units = 1;     /* mtex off => single TMU */
int gl_stencilbits = 0;
modestate_t modestate = MS_UNINIT;
cvar_t vid_gamma = { "gamma", "1", CVAR_ARCHIVE };
cvar_t vid_contrast = { "contrast", "1", CVAR_ARCHIVE };

static int      fbfd = -1;
static int      ttyfd = -1;         /* /dev/tty0 (or /dev/console) for the fbcon mode switch */
static uint32_t *readbuf = NULL;    /* W*H RGBA from glReadPixels */
static uint32_t *fbimg = NULL;      /* W*H, y-flipped, for /dev/fb0 */

/* DOS-style mode switch: tell the HDMI text console (pl011-tty fbcon) to stop drawing to
 * the framebuffer while this full-screen GPU app owns it. Console output is not lost — it
 * accumulates in an off-screen shadow and reappears (with everything printed meanwhile)
 * when we restore FBCON_ENABLED at shutdown. Without this the klog/psh mirror overdraws
 * our rendered frame. */
static void console_setmode(int mode)
{
	if (ttyfd < 0) {
		ttyfd = open("/dev/tty0", O_RDWR);
		if (ttyfd < 0)
			ttyfd = open("/dev/console", O_RDWR);
	}
	if (ttyfd >= 0) {
		if (ioctl(ttyfd, FBCONSETMODE, mode) == 0)
			Sys_Printf("VID: HDMI console fbcon mode -> %d\n", mode);
		else
			Sys_Printf("VID: FBCONSETMODE(%d) failed (console may overdraw)\n", mode);
	}
	else {
		Sys_Printf("VID: no /dev/tty0|console to switch fbcon mode\n");
	}
}

/* Continuous re-blit: caches-off Quake renders only a few frames per minute, so a
 * once-per-frame blit loses to the kernel klog mirror (fbcon), which overdraws /dev/fb0
 * between frames. A helper thread re-writes the last rendered frame (fbimg) at ~3 Hz so
 * the held frame stays on screen. The thread does NO GL/Mesa calls (only write()), so the
 * GL-on-main-thread TLS invariant is preserved. GL_EndRendering only updates fbimg. */
static pthread_t        reblit_thread;
static volatile int     reblit_run = 0;

static void *reblit_fn(void *arg)
{
	(void)arg;
	while (reblit_run) {
		if (fbfd >= 0 && fbimg) {
			lseek(fbfd, 0, SEEK_SET);
			(void)write(fbfd, fbimg, (size_t)VID_W * VID_H * 4);
		}
		usleep(300000);     /* ~3 Hz */
	}
	return NULL;
}

void VID_Init(void)
{
	if (qsv3d_init(VID_W, VID_H) != 0)
		Sys_Error("VID_Init: V3D GL context create failed");
	qsv3d_make_current();

	vid.width = vid.conwidth = VID_W;
	vid.height = vid.conheight = VID_H;
	vid.aspect = (float)VID_W / (float)VID_H;
	vid.numpages = 1;
	vid.recalc_refdef = 1;

	/* Wire the engine colormap (Host_Init loads host_colormap from gfx/colormap.lmp
	 * BEFORE calling VID_Init). gl_vidsdl.c does this; without it vid.colormap stays NULL
	 * and CL_NewTranslation memcpy's from NULL -> Data Abort (far=0) when a player's skin
	 * translation is built during demo/3D playback. */
	{
		extern byte *host_colormap;
		vid.colormap = host_colormap;
		vid.fullbright = 256 - LittleLong(*((int *)vid.colormap + 2048));
	}

	readbuf = (uint32_t *)malloc((size_t)VID_W * VID_H * 4);
	fbimg = (uint32_t *)malloc((size_t)VID_W * VID_H * 4);
	if (fbimg)
		memset(fbimg, 0, (size_t)VID_W * VID_H * 4);
	fbfd = open("/dev/fb0", O_WRONLY);
	if (fbfd < 0)
		Sys_Printf("VID_Init: /dev/fb0 open failed (rendering continues offscreen)\n");

	if (fbfd >= 0 && fbimg) {
		reblit_run = 1;
		pthread_create(&reblit_thread, NULL, reblit_fn, NULL);
	}

	/* Claim the framebuffer: switch the HDMI text console into "graphics mode". */
	console_setmode(FBCON_DISABLED);

	Sys_Printf("VID_Init: V3D GL %dx%d, /dev/fb0 %s\n",
	           VID_W, VID_H, (fbfd >= 0) ? "open" : "unavailable");
}

void VID_Shutdown(void)
{
	/* Hand the framebuffer back to the text console (it redraws with all the klog/psh
	 * output that accumulated while we owned the screen). */
	console_setmode(FBCON_ENABLED);
	if (ttyfd >= 0) {
		close(ttyfd);
		ttyfd = -1;
	}
	reblit_run = 0;
	if (fbfd >= 0) {
		close(fbfd);
		fbfd = -1;
	}
}

/* Quake calls these around each frame's GL work. */
void GL_BeginRendering(int *x, int *y, int *width, int *height)
{
	extern void qsv3d_bind_fbo(void);
	qsv3d_bind_fbo();   /* render this frame into our readable FBO, not default FB0 */
	*x = 0;
	*y = 0;
	*width = vid.width;
	*height = vid.height;
}

void GL_EndRendering(void)
{
	int sy;

	if (fbfd < 0 || !readbuf || !fbimg)
		return;

	{ extern void qsv3d_bind_fbo(void); qsv3d_bind_fbo(); }  /* read from our FBO, not FB0 */
	glReadBuffer(GL_COLOR_ATTACHMENT0);
	glFinish();
	glReadPixels(0, 0, vid.width, vid.height, GL_RGBA, GL_UNSIGNED_BYTE, readbuf);

	/* y-flip (GL y-up -> screen y-down) into fbimg; the re-blit thread writes it to
	 * /dev/fb0 at ~3 Hz (holds the frame on screen against the fbcon klog mirror). */
	for (sy = 0; sy < vid.height; sy++)
		memcpy(fbimg + (size_t)sy * vid.width,
		       readbuf + (size_t)(vid.height - 1 - sy) * vid.width,
		       (size_t)vid.width * 4);
}

/* --- the remaining VID_* contract: trivial for a fixed single fullscreen mode --- */
void VID_Shutdown(void);
qboolean VID_HasMouseOrInputFocus(void) { return true; }
qboolean VID_IsMinimized(void) { return false; }
void VID_Lock(void) {}
void VID_Toggle(void) {}
void VID_SyncCvars(void) {}
