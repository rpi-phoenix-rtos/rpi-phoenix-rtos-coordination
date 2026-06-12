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
static uint32_t *readbuf = NULL;    /* W*H RGBA from glReadPixels */
static uint32_t *fbimg = NULL;      /* W*H, y-flipped, for /dev/fb0 */

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

	readbuf = (uint32_t *)malloc((size_t)VID_W * VID_H * 4);
	fbimg = (uint32_t *)malloc((size_t)VID_W * VID_H * 4);
	fbfd = open("/dev/fb0", O_WRONLY);
	if (fbfd < 0)
		Sys_Printf("VID_Init: /dev/fb0 open failed (rendering continues offscreen)\n");

	Sys_Printf("VID_Init: V3D GL %dx%d, /dev/fb0 %s\n",
	           VID_W, VID_H, (fbfd >= 0) ? "open" : "unavailable");
}

void VID_Shutdown(void)
{
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

	/* y-flip (GL y-up -> screen y-down), 1:1 native blit to /dev/fb0. */
	for (sy = 0; sy < vid.height; sy++)
		memcpy(fbimg + (size_t)sy * vid.width,
		       readbuf + (size_t)(vid.height - 1 - sy) * vid.width,
		       (size_t)vid.width * 4);

	lseek(fbfd, 0, SEEK_SET);
	(void)write(fbfd, fbimg, (size_t)vid.width * vid.height * 4);
}

/* --- the remaining VID_* contract: trivial for a fixed single fullscreen mode --- */
void VID_Shutdown(void);
qboolean VID_HasMouseOrInputFocus(void) { return true; }
qboolean VID_IsMinimized(void) { return false; }
void VID_Lock(void) {}
void VID_Toggle(void) {}
void VID_SyncCvars(void) {}
