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
/* Pull in the GL extension function prototypes (glGenBuffers/glActiveTexture/...) so we can
 * take their addresses to wire Quakespasm's GL_*Func pointers. Must precede the GL headers
 * that quakedef.h -> glquake.h -> the SDL shim include. */
#define GL_GLEXT_PROTOTYPES 1

#include "quakedef.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/ioctl.h>

/* rpi4-fb client ABI (replicated to avoid a devices-tree include path); MUST match
 * sources/phoenix-rtos-devices/video/rpi4-fb/rpi4-fb.h so the _IOR command number agrees. */
typedef struct {
	uint16_t width, height, bpp, pitch;
	uint64_t smemlen;     /* framebuffer size in bytes (pitch*height) */
	uint64_t framebuffer; /* physical base address */
} rpi4fb_mode_t;
#define RPI4FB_GETMODE _IOR('g', 1, rpi4fb_mode_t)

/* winsys (v3d_phoenix_winsys.c): point render-to-scanout at the HDMI framebuffer, and
 * query whether the full-screen RT actually got backed by it. */
extern void v3d_phoenix_set_scanout(uint32_t pa, uint32_t bytes);
extern int v3d_phoenix_scanout_init(uint32_t pa, uint32_t w, uint32_t h, uint32_t pitch); /* detects double-buffer */
extern int v3d_phoenix_scanout_active(void);
#include <sys/ioctl.h>
#include <phoenix/fbcon.h>

/* GL context/FBO helpers (pl_phoenix_glctx.c) — plain C API, no Quake/Mesa types. */
int  qsv3d_init(int w, int h);
void qsv3d_make_current(void);
int  qsv3d_resolve(void);   /* color-only GPU blit: DRAM render FBO -> scanout fb (returns 1 if active) */

#define VID_W 1920
#define VID_H 1080

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
static uint32_t *readbuf[2] = { NULL, NULL };  /* double-buffered glReadPixels targets (pipelined present) */
static uint32_t *fbimg = NULL;      /* W*H, y-flipped+gamma, written to /dev/fb0 by the present thread */

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
static volatile unsigned fb_seq = 0;   /* bumped per presented frame; reblit refreshes only when idle */

/* Present pipeline: the render (GL) thread fills readbuf[idx] via glReadPixels (GL ops must stay
 * on the GL thread) and hands the index to the present thread, which does the CPU-only y-flip +
 * gamma blit and the /dev/fb0 write on another core (SMP) — overlapping ~17 ms of present work
 * with the next frame's render. Pure threading; no GL/coherency change, visually verifiable. */
static pthread_mutex_t  present_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t   present_cv = PTHREAD_COND_INITIALIZER;
static int              g_ready = -1;     /* readbuf index handed to the present thread, or -1 */
static int              g_inflight = -1;  /* readbuf index the present thread is blitting, or -1 */

/* CPU y-flip (GL y-up -> screen y-down) + brighten-gamma readbuf[idx] -> fbimg. Runs on the
 * present thread, off the render thread. (gamma<1 lifts the dark V3D world; barely touches the
 * already-bright HUD. TODO: root-cause the darkness + retune/remove, see project_quakespasm_port.) */
static void present_blit(int idx)
{
	static unsigned char glut[256];
	static int glut_done = 0;
	int sy, px;

	if (!glut_done) {
		for (px = 0; px < 256; px++) {
			double v = pow((double)px / 255.0, 0.5) * 255.0;   /* gamma 0.5 = brighten */
			glut[px] = (v > 255.0) ? 255u : (unsigned char)(v + 0.5);
		}
		glut_done = 1;
	}
	for (sy = 0; sy < VID_H; sy++) {
		const unsigned char *s = (const unsigned char *)(readbuf[idx] + (size_t)(VID_H - 1 - sy) * VID_W);
		unsigned char *d = (unsigned char *)(fbimg + (size_t)sy * VID_W);
		for (px = 0; px < VID_W; px++) {
			d[px * 4 + 0] = glut[s[px * 4 + 0]];
			d[px * 4 + 1] = glut[s[px * 4 + 1]];
			d[px * 4 + 2] = glut[s[px * 4 + 2]];
			d[px * 4 + 3] = s[px * 4 + 3];
		}
	}
}

static void present_fb0(void)
{
	/* In render-to-scanout mode the GPU owns /dev/fb0; writing the CPU fbimg here would
	 * clobber the rendered frame. (Guards the idle-refresh path if scanout engaged lazily.) */
	if (v3d_phoenix_scanout_active())
		return;
	if (fbfd >= 0 && fbimg) {
		lseek(fbfd, 0, SEEK_SET);
		(void)write(fbfd, fbimg, (size_t)VID_W * VID_H * 4);
	}
}

/* Present + idle-refresh consumer thread. Blocks on present_cv for a frame the render thread
 * hands off (g_ready); on a ~100 ms timeout with no new frame, refreshes the held frame so the
 * shared klog/fbcon mirror can't leave the screen stale. */
static void *reblit_fn(void *arg)
{
	int idle_ticks = 0;
	(void)arg;

	pthread_mutex_lock(&present_lock);
	while (reblit_run) {
		while (reblit_run && g_ready < 0) {
			struct timespec ts;
			clock_gettime(CLOCK_REALTIME, &ts);
			ts.tv_nsec += 100000000L;   /* 100 ms idle watch */
			if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
			if (pthread_cond_timedwait(&present_cv, &present_lock, &ts) != 0 && g_ready < 0) {
				if (++idle_ticks >= 5) {   /* ~500 ms idle -> repaint held frame */
					pthread_mutex_unlock(&present_lock);
					present_fb0();
					pthread_mutex_lock(&present_lock);
					idle_ticks = 0;
				}
			}
		}
		if (!reblit_run)
			break;

		/* A frame is ready: claim it, free the handoff slot, blit + present outside the lock. */
		{
			int idx = g_ready;
			g_inflight = idx;
			g_ready = -1;
			pthread_cond_signal(&present_cv);   /* render thread may reuse the handoff slot */
			pthread_mutex_unlock(&present_lock);

			present_blit(idx);
			present_fb0();

			pthread_mutex_lock(&present_lock);
			g_inflight = -1;
			fb_seq++;
			idle_ticks = 0;
			pthread_cond_signal(&present_cv);   /* buffer freed; wake render thread if waiting */
		}
	}
	pthread_mutex_unlock(&present_lock);
	return NULL;
}

void VID_Init(void)
{
	/* SCTLR_EL1.UCI selftest: prove EL0 can run cache-maintenance (dc civac), which the
	 * cacheable-readback fast path needs. If UCI is not set this instruction traps -> a
	 * visible EL0 exception in the UART dump rather than this OK line. */
	{
		volatile char uci_probe[64] __attribute__((aligned(64)));
		uci_probe[0] = 0;
		__asm__ volatile("dc civac, %0" :: "r"(&uci_probe[0]) : "memory");
		__asm__ volatile("dsb ish" ::: "memory");
		Sys_Printf("PL_VID: EL0 dc civac OK — SCTLR.UCI active (cacheable-DMA cache ops available)\n");
	}

	/* Open /dev/fb0 and hand the GPU winsys the scanout framebuffer's PA + size BEFORE
	 * qsv3d_init creates the FBO — the full-screen render target is allocated during context
	 * create, so the winsys must already know the scanout PA to back the RT with it
	 * (render-to-scanout: GPU stores straight to screen, no CPU readback/blit). */
	fbfd = open("/dev/fb0", O_WRONLY);
	if (fbfd < 0)
		Sys_Printf("VID_Init: /dev/fb0 open failed (rendering continues offscreen)\n");
	if (fbfd >= 0) {
		rpi4fb_mode_t mode;
		if (ioctl(fbfd, RPI4FB_GETMODE, &mode) == 0 && mode.framebuffer != 0) {
			int sc = v3d_phoenix_scanout_init((uint32_t)mode.framebuffer, mode.width, mode.height, mode.pitch);
			Sys_Printf("VID_Init: scanout fb PA=0x%08x %ux%u pitch=%u size=%u -> %s\n",
			           (uint32_t)mode.framebuffer, mode.width, mode.height,
			           mode.pitch, (uint32_t)mode.smemlen,
			           (sc == 2) ? "double-buffer+page-flip" : "single (blit-resolve)");
		}
		else {
			Sys_Printf("VID_Init: RPI4FB_GETMODE failed — render-to-scanout disabled\n");
		}
	}

	if (qsv3d_init(VID_W, VID_H) != 0)
		Sys_Error("VID_Init: V3D GL context create failed");
	qsv3d_make_current();

	vid.width = vid.conwidth = VID_W;
	vid.height = vid.conheight = VID_H;
	vid.aspect = (float)VID_W / (float)VID_H;
	vid.numpages = 1;
	vid.recalc_refdef = 1;

	/* Enable the GL extensions Quakespasm uses. These are core in our GL 2.1 / V3D 4.2
	 * context; wire the GL_*Func pointers + flip the gl_*_able flags Quakespasm gates on.
	 *  - VBO: brush + alias vertex buffers. The world-surface texturing path needs this;
	 *    in the all-basic config the brush texcoords came from an undriven path and sampled
	 *    black. Also cuts per-frame CPU (higher fps).
	 *  - Multitexture: texture + lightmap in ONE pass (vs the slow multipass), and with
	 *    env_combine enables one-pass 2x-overbright -> a correctly-lit world + higher fps.
	 *  - GenerateMipmap: real mip chains for world/model textures. */
	GL_GenBuffersFunc    = (PFNGLGENBUFFERSARBPROC)glGenBuffers;
	GL_BindBufferFunc    = (PFNGLBINDBUFFERARBPROC)glBindBuffer;
	GL_BufferDataFunc    = (PFNGLBUFFERDATAARBPROC)glBufferData;
	GL_BufferSubDataFunc = (PFNGLBUFFERSUBDATAARBPROC)glBufferSubData;
	GL_DeleteBuffersFunc = (PFNGLDELETEBUFFERSARBPROC)glDeleteBuffers;
	gl_vbo_able = true;

	GL_MTexCoord2fFunc         = (PFNGLMULTITEXCOORD2FARBPROC)glMultiTexCoord2f;
	GL_SelectTextureFunc       = (PFNGLACTIVETEXTUREARBPROC)glActiveTexture;
	GL_ClientActiveTextureFunc = (PFNGLCLIENTACTIVETEXTUREARBPROC)glClientActiveTexture;
	gl_max_texture_units = 1;
	glGetIntegerv(GL_MAX_TEXTURE_UNITS, &gl_max_texture_units);
	if (gl_max_texture_units >= 2) {
		gl_mtexable = true;
		gl_texture_env_combine = true;   /* one-pass overbright via GL_RGB_SCALE */
		gl_texture_env_add = true;
	}

	GL_GenerateMipmap = (QS_PFNGENERATEMIPMAP)glGenerateMipmap;

	/* GLSL — the modern world renderer. The FF multitexture path renders the world flat
	 * gray (the texenv texture*lightmap combiner doesn't produce correct output on V3D);
	 * the GLSL world shader samples texture+lightmap+overbright correctly with VBO geometry.
	 * This is the advanced/correct/fast path (full bring-up). Shaders compile via Mesa's
	 * GLSL frontend -> NIR -> v3d_compile in libGL-phoenix. If a shader fails to compile,
	 * Quakespasm leaves r_world_program=0 and falls back to the FF path (no crash). Alias
	 * models stay on the (working) FF path for now (gl_glsl_alias_able left false). */
	GL_CreateShaderFunc             = (QS_PFNGLCREATESHADERPROC)glCreateShader;
	GL_DeleteShaderFunc             = (QS_PFNGLDELETESHADERPROC)glDeleteShader;
	GL_DeleteProgramFunc            = (QS_PFNGLDELETEPROGRAMPROC)glDeleteProgram;
	GL_ShaderSourceFunc             = (QS_PFNGLSHADERSOURCEPROC)glShaderSource;
	GL_CompileShaderFunc            = (QS_PFNGLCOMPILESHADERPROC)glCompileShader;
	GL_GetShaderivFunc              = (QS_PFNGLGETSHADERIVPROC)glGetShaderiv;
	GL_GetShaderInfoLogFunc         = (QS_PFNGLGETSHADERINFOLOGPROC)glGetShaderInfoLog;
	GL_GetProgramivFunc             = (QS_PFNGLGETPROGRAMIVPROC)glGetProgramiv;
	GL_GetProgramInfoLogFunc        = (QS_PFNGLGETPROGRAMINFOLOGPROC)glGetProgramInfoLog;
	GL_CreateProgramFunc            = (QS_PFNGLCREATEPROGRAMPROC)glCreateProgram;
	GL_AttachShaderFunc             = (QS_PFNGLATTACHSHADERPROC)glAttachShader;
	GL_LinkProgramFunc              = (QS_PFNGLLINKPROGRAMPROC)glLinkProgram;
	GL_BindAttribLocationFunc       = (QS_PFNGLBINDATTRIBLOCATIONFUNC)glBindAttribLocation;
	GL_UseProgramFunc               = (QS_PFNGLUSEPROGRAMPROC)glUseProgram;
	GL_GetAttribLocationFunc        = (QS_PFNGLGETATTRIBLOCATIONPROC)glGetAttribLocation;
	GL_VertexAttribPointerFunc      = (QS_PFNGLVERTEXATTRIBPOINTERPROC)glVertexAttribPointer;
	GL_EnableVertexAttribArrayFunc  = (QS_PFNGLENABLEVERTEXATTRIBARRAYPROC)glEnableVertexAttribArray;
	GL_DisableVertexAttribArrayFunc = (QS_PFNGLDISABLEVERTEXATTRIBARRAYPROC)glDisableVertexAttribArray;
	GL_GetUniformLocationFunc       = (QS_PFNGLGETUNIFORMLOCATIONPROC)glGetUniformLocation;
	GL_Uniform1iFunc                = (QS_PFNGLUNIFORM1IPROC)glUniform1i;
	GL_Uniform1fFunc                = (QS_PFNGLUNIFORM1FPROC)glUniform1f;
	GL_Uniform3fFunc                = (QS_PFNGLUNIFORM3FPROC)glUniform3f;
	GL_Uniform4fFunc                = (QS_PFNGLUNIFORM4FPROC)glUniform4f;
	gl_glsl_able = true;
	/* CRITICAL: the world GLSL program (r_world_program) is only built when gl_glsl_alias_able
	 * is set (r_world.c:898 `if (!gl_glsl_alias_able) return;` before GL_CreateProgram). With it
	 * false, r_world_program stays 0 -> the world falls back to the FF multitexture path, which
	 * renders flat gray on V3D (FF texenv combiner bug). The host reference has it enabled
	 * ("Enabled: GLSL alias model rendering"). Enable it so the GLSL world (+ alias) renders. */
	gl_glsl_alias_able = true;

	Sys_Printf("VID: GL exts wired: vbo=%d mtex=%d env_combine=%d glsl=%d max_tmu=%d\n",
	           (int)gl_vbo_able, (int)gl_mtexable, (int)gl_texture_env_combine,
	           (int)gl_glsl_able, (int)gl_max_texture_units);

	/* CRITICAL: gl_vidsdl.c's GL_SetupState (which this port replaces) sets the cull winding
	 * to glCullFace(GL_BACK)+glFrontFace(GL_CW) — quakespasm winds its world brush faces for
	 * CW-front. Without this the default glFrontFace(GL_CCW) treats every world poly as a back
	 * face, so R_SetupGL's glEnable(GL_CULL_FACE) culls the ENTIRE world (alias models survive
	 * on their own winding). Replicate it here so the world renders.
	 *
	 * For render-to-scanout we force Mesa fb_orientation Y_0_TOP (st_atom_framebuffer.c) so the
	 * viewport Y is negated and the frame lands upright on the y-down HDMI scanout. Mesa keeps
	 * front-face sense consistent across that Y-flip (front-face is resolved before the viewport
	 * Y-negate), so glFrontFace stays GL_CW to match quakespasm's CW-wound world brushes. Setting
	 * GL_CCW here culls the entire world (gray-world bug) — verified on HW. So: Y_0_TOP for the
	 * flip, GL_CW unchanged for winding. */
	glCullFace (GL_BACK);
	glFrontFace (GL_CW);

	/* CRITICAL: replicate the REST of gl_vidsdl.c's GL_SetupState, which this port replaces. The
	 * port previously set only the cull winding above and dropped the rest — most importantly the
	 * ALPHA-TEST and BLEND state. Quakespasm sets these ONCE here and relies on them globally
	 * thereafter (Draw_Character/sbar/particles only toggle glEnable(GL_ALPHA_TEST|GL_BLEND), never
	 * re-set the func/ref). Without them:
	 *  - glAlphaFunc defaults to (GL_ALWAYS, 0) so GL_ALPHA_TEST never discards transparent texels
	 *    -> alpha-keyed textures (conchars/HUD digits/sprites/fence brushes) render their fully-
	 *    transparent texels as OPAQUE -> solid-color boxes behind HUD numbers, unreadable fonts.
	 *  - glBlendFunc defaults to (GL_ONE, GL_ZERO) so GL_BLEND ignores source alpha -> particles
	 *    (sparks/blood/explosions) draw as solid filled triangles instead of soft alpha sprites.
	 * Matching stock GL_SetupState fixes all of these (the V3D R/B-swap is orthogonal, handled in
	 * the framebuffer path). */
	glClearColor (0.15f, 0.15f, 0.15f, 0.0f);
	glEnable (GL_TEXTURE_2D);
	glEnable (GL_ALPHA_TEST);
	glAlphaFunc (GL_GREATER, 0.666f);
	glShadeModel (GL_FLAT);
	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	/* Stock GL_LEQUAL. This was temporarily GL_LESS to dodge a depth-drain wedge, but that wedge
	 * was the disabled-Early-Z + L2T-flush-race interaction; with EZ re-enabled (the flush race is
	 * fixed in the winsys), GL_LEQUAL runs without wedging and EZ early-rejects overdraw. The
	 * viewmodel GL_LEQUAL special-case in R_DrawViewModel is correspondingly removed. */
	glDepthFunc (GL_LEQUAL);

	/* CRITICAL: the engine builds the GLSL world + alias programs in gl_vidsdl.c's GL_Init —
	 * which THIS port replaces, so they were never created (r_world_program stayed 0) and the
	 * world fell back to the gray FF path. Build them here, now that gl_glsl_able/
	 * gl_glsl_alias_able and the GLSL function pointers are wired. */
	{
		extern void GLAlias_CreateShaders (void);
		extern void GLWorld_CreateShaders (void);
		GLAlias_CreateShaders ();
		GLWorld_CreateShaders ();
	}

	/* Wire the engine colormap (Host_Init loads host_colormap from gfx/colormap.lmp
	 * BEFORE calling VID_Init). gl_vidsdl.c does this; without it vid.colormap stays NULL
	 * and CL_NewTranslation memcpy's from NULL -> Data Abort (far=0) when a player's skin
	 * translation is built during demo/3D playback. */
	{
		extern byte *host_colormap;
		vid.colormap = host_colormap;
		vid.fullbright = 256 - LittleLong(*((int *)vid.colormap + 2048));
	}

	/* Fallback CPU-present buffers (only used when render-to-scanout is NOT active). */
	readbuf[0] = (uint32_t *)malloc((size_t)VID_W * VID_H * 4);
	readbuf[1] = (uint32_t *)malloc((size_t)VID_W * VID_H * 4);
	fbimg = (uint32_t *)malloc((size_t)VID_W * VID_H * 4);
	if (fbimg)
		memset(fbimg, 0, (size_t)VID_W * VID_H * 4);

	/* Start the present/idle-refresh thread only in the fallback (CPU readback) path. With
	 * render-to-scanout the GPU owns the framebuffer, so the idle-refresh would clobber it
	 * with the (empty) fbimg — skip the thread entirely. */
	if (fbfd >= 0 && fbimg && !v3d_phoenix_scanout_active()) {
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
	pthread_mutex_lock(&present_lock);
	pthread_cond_signal(&present_cv);   /* wake the present thread out of its wait */
	pthread_mutex_unlock(&present_lock);
	pthread_join(reblit_thread, NULL);  /* ensure it stops touching fbfd before we close it */
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
	static int cur = 0;
	double ts_a, ts_b, ts_c, ts_d;

	if (fbfd < 0 || !readbuf[0] || !readbuf[1] || !fbimg)
		return;

	{ extern void qsv3d_bind_fbo(void); qsv3d_bind_fbo(); }  /* read from our FBO, not FB0 */
	glReadBuffer(GL_COLOR_ATTACHMENT0);
	ts_a = Sys_DoubleTime();
	glFinish();   /* submit + synchronously render the frame */
	ts_b = Sys_DoubleTime();

	if (v3d_phoenix_scanout_active()) {
		/* RESOLVE-TO-SCANOUT: Quake rendered the depth-tested frame into a DRAM color RT (off
		 * the live fb, so the depth-output pipeline never contended with the HVS display reads
		 * that caused the intermittent wedge). Now a color-only GPU blit resolves it to the
		 * scanout fb — no depth FIFO, one light streaming pass, so it cannot hit that stall, and
		 * still no CPU readback. The frame lands UPRIGHT: both FBOs are full-screen so Mesa
		 * forced Y_0_TOP on both, and the (0..h)->(0..h) blit copies verbatim onto the y-down
		 * scanout, with winding compensated by glFrontFace(GL_CCW) below. */
		qsv3d_resolve();
		ts_c = ts_b;
		ts_d = Sys_DoubleTime();
	}
	else {
		/* Fallback (scanout unavailable): copy the FBO out and present via the CPU pipeline. */

		/* Don't refill a buffer the present thread is still reading. With the present thread
		 * (~17 ms) faster than this thread's finish+readpx (~29 ms), this rarely blocks. */
		pthread_mutex_lock(&present_lock);
		while ((g_ready == cur) || (g_inflight == cur))
			pthread_cond_wait(&present_cv, &present_lock);
		pthread_mutex_unlock(&present_lock);

		glReadPixels(0, 0, vid.width, vid.height, GL_RGBA, GL_UNSIGNED_BYTE, readbuf[cur]);
		ts_c = Sys_DoubleTime();

		/* Hand the filled buffer to the present thread; it does the y-flip+gamma blit and the
		 * /dev/fb0 write on another core, overlapping with this thread's NEXT frame. */
		pthread_mutex_lock(&present_lock);
		while (g_ready >= 0)
			pthread_cond_wait(&present_cv, &present_lock);
		g_ready = cur;
		pthread_cond_signal(&present_cv);
		pthread_mutex_unlock(&present_lock);
		cur ^= 1;

		ts_d = Sys_DoubleTime();
	}

	/* Frame-rate + main-thread attribution self-log (UART). The blit + fb0 write now run on
	 * the present thread (off this thread), so the render-thread frame is finish + readpx +
	 * handoff-wait; FPS tracks that. Prints every ~2 s. */
	{
		static double fps_t0 = 0.0;
		static double acc_fin = 0.0, acc_read = 0.0, acc_hand = 0.0;
		static int fps_frames = 0;
		double now = ts_d;

		fps_frames++;
		acc_fin += ts_b - ts_a;
		acc_read += ts_c - ts_b;
		acc_hand += ts_d - ts_c;
		if (fps_t0 == 0.0) {
			fps_t0 = now;
		}
		else if ((now - fps_t0) >= 2.0) {
			Sys_Printf("QSFPS: %.1f fps (%d fr/%.2fs) main/fr: finish=%.2fms readpx=%.2fms handoff=%.2fms (blit+fb0 off-thread)\n",
				(double)fps_frames / (now - fps_t0), fps_frames, now - fps_t0,
				1000.0 * acc_fin / fps_frames, 1000.0 * acc_read / fps_frames,
				1000.0 * acc_hand / fps_frames);
			fps_t0 = now;
			fps_frames = 0;
			acc_fin = acc_read = acc_hand = 0.0;
		}
	}
}

/* --- the remaining VID_* contract: trivial for a fixed single fullscreen mode --- */
void VID_Shutdown(void);
qboolean VID_HasMouseOrInputFocus(void) { return true; }
qboolean VID_IsMinimized(void) { return false; }
void VID_Lock(void) {}
void VID_Toggle(void) {}
void VID_SyncCvars(void) {}
