/*
 * pl_phoenix_main.c — Phoenix entry point + host loop for Quakespasm: replaces
 * main_sdl.c. No SDL init; just COM_InitArgv -> Sys_Init -> Host_Init, then the
 * Host_Frame loop driven by Sys_DoubleTime.
 */
#include "quakedef.h"

#include <stdlib.h>
#include <unistd.h>
#include <string.h>

/* Shareware Quake needs far less than the upstream 256 MB default; use a modest
 * heap that Phoenix can reliably back, and memset it after malloc to force every
 * page committed/mapped up front (the hunk faulted at a low offset during BSP load,
 * i.e. untouched malloc pages weren't mapped). */
#define DEFAULT_MEMORY (96 * 1024 * 1024)

/* The host runs on the MAIN thread (not a pthread). Quake has large stack frames +
 * recursive renderers, so the main-thread stack is enlarged via PT_GNU_STACK in the
 * link (-z stack-size=... in misc/rpi4-quake/Makefile). Running on the main thread is
 * deliberate: Mesa's glapi dispatch is TLS and the kernel sets up TLS for the main
 * thread (process.c), whereas GL on a libphoenix pthread faulted in the dispatch
 * (far=0x100030428) — the rpi4-glcube demo runs identical GL on the main thread fine. */

static quakeparms_t parms;      /* host_parms (the pointer) is owned by host.c */

/* Discovered basedir (set by wait_for_gamedata): "<basedir>/id1/pak0.pak" is the data. */
static const char *g_basedir = "/";

/* Wait (bounded) for the game data to be reachable before Host_Init, and discover WHERE
 * it lives. The same NFS export appears at different paths depending on the boot variant:
 *  - netboot variant: mounted at /nfstest (deterministic, ~0.5 s) -> data at /nfstest/id1
 *  - nfsroot variant: nfs-fs takeover registers it as "/" -> data at /id1, BUT a syspage-
 *    launched process can race the takeover and keep a stale (dummyfs) root that never sees
 *    /id1 (observed: 30 s of polling /id1 failed even after "registered / (takeover)").
 * So probe BOTH candidate basedirs each poll and adopt whichever first exposes id1/pak0.pak.
 * Also covers the libnfs first-read dircache ENOENT (#156): a retry succeeds. */
static void wait_for_gamedata(void)
{
	static const char *cands[] = { "/nfstest", "/" };
	char path[80];
	int i, c;
	for (i = 0; i < 360; i++) {     /* ~180 s — NFS mount + DHCP can be slow/variable (#156) */
		for (c = 0; c < (int)(sizeof(cands) / sizeof(cands[0])); c++) {
			FILE *f;
			snprintf(path, sizeof(path), "%s/id1/pak0.pak", cands[c]);
			f = fopen(path, "rb");
			if (f) {
				fclose(f);
				g_basedir = cands[c];
				Sys_Printf("quakespasm: found %s after %d tries (basedir=%s)\n",
				           path, i + 1, g_basedir);
				return;
			}
		}
		usleep(500000);
	}
	Sys_Printf("quakespasm: pak0.pak not found after wait (continuing; Host_Init will report)\n");
}

int main(int argc, char *argv[])
{
	double time, oldtime, newtime;

	/* Unbuffered stdout so prints reach the UART immediately (psh stdout may be
	 * fully buffered -> buffered output is lost if the process exits/faults early). */
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);
	printf("quakespasm: main() entered (argc=%d)\n", argc);

	host_parms = &parms;
	parms.basedir = "/";    /* absolute: on nfsroot, /id1/pak0.pak is the NFS export */
	parms.argc = argc;
	parms.argv = argv;
	parms.errstate = 0;

	COM_InitArgv(parms.argc, parms.argv);

	isDedicated = (COM_CheckParm("-dedicated") != 0);

	Sys_Init();

	Sys_Printf("Initializing QuakeSpasm (Phoenix/V3D)\n");

	parms.memsize = DEFAULT_MEMORY;
	parms.membase = malloc(parms.memsize);
	if (!parms.membase)
		Sys_Error("Not enough memory free; check disk space\n");
	/* Touch every page so the whole hunk is committed/mapped (Phoenix does not
	 * demand-page large anonymous malloc: untouched pages translation-fault). */
	memset(parms.membase, 0, parms.memsize);
	Sys_Printf("quakespasm: heap %d MB committed at %p\n",
	           (int)(parms.memsize >> 20), parms.membase);

	wait_for_gamedata();
	parms.basedir = g_basedir;      /* whichever path exposed id1/pak0.pak */

	Sys_Printf("Host_Init\n");
	Host_Init();

	/* Let the attract demo play (cl_startdemos default = 1 -> demo1.dem, a recorded E1M3
	 * walkthrough). This is the CLIENT-ONLY 3D path: it renders the real Quake world from the
	 * recorded demo without spawning a server / running the QuakeC VM (which "map start"
	 * currently crashes in). With the MMU TLB-flush fix the 3D frames render to the V3D, and
	 * with the 1MB NFS readmax the pak0 load is faster. (The BSP/lightmap build is still
	 * CPU-bound with caches off (TD-16) — that is the remaining wall for fast 3D load.) */

	/* KNOWN GAP (next session): world brush-surface geometry renders, but textured world
	 * surfaces sample black (r_drawflat shows flat-colored geometry fine; r_fullbright and
	 * gl_texturemode GL_LINEAR both stay black -> not lightmap/mipmap/overbright). Entities,
	 * weapon, items, HUD all texture correctly via the same 8-bit/d_8to24 palette path, so
	 * this is specific to the BSP brush-surface texturing path (likely the VBO/texcoord
	 * route that the all-gl_*_able=false basic config does not drive). Deferred. */

	oldtime = Sys_DoubleTime();
	while (1) {
		newtime = Sys_DoubleTime();
		time = newtime - oldtime;
		Host_Frame(time);
		oldtime = newtime;
		usleep(1000);
	}
	return 0;
}
