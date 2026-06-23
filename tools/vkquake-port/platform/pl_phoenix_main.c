/*
 * pl_phoenix_main.c — Phoenix entry point + host loop for vkQuake: replaces main_sdl.c.
 *
 * No SDL init. Just COM_InitArgv -> Sys_Init -> Host_Init, then the Host_Frame loop
 * driven by Sys_DoubleTime. Adapted from quakespasm-port/platform/pl_phoenix_main.c.
 *
 * KEY DIFFERENCE vs. the quakespasm port: vkQuake's quakeparms_t has NO membase/memsize
 * fields — the engine uses its own mem.c (mimalloc-style) allocator, not the classic
 * Quake hunk. So this main does NOT pre-allocate/commit a heap; it just discovers basedir
 * and runs the host loop. (The upstream main_sdl.c host loop calls VID_HasMouseOrInputFocus
 * / VID_IsMinimized / `listening` for input-focus sleeps; those belong to the not-yet-
 * written Vulkan vid shim, so we use a plain fixed-cadence loop here to avoid pulling new
 * undefined symbols into the link. The vid shim can reinstate the focus-aware sleeps later.)
 */
#include "quakedef.h"

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

static quakeparms_t parms;      /* host_parms (the pointer) is owned by host.c */

/* Discovered basedir (set by wait_for_gamedata): "<basedir>/id1/pak0.pak" is the data. */
static const char *g_basedir = "/";

/* Wait (bounded) for the game data to be reachable before Host_Init, and discover WHERE
 * it lives — same NFS-path race as the quakespasm port (netboot mounts at /nfstest;
 * nfsroot takeover registers it as "/"). Probe both candidate basedirs each poll and
 * adopt whichever first exposes id1/pak0.pak. Also absorbs the libnfs first-read dircache
 * ENOENT (#156): a retry succeeds. */
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
				Sys_Printf("vkquake: found %s after %d tries (basedir=%s)\n",
				           path, i + 1, g_basedir);
				return;
			}
		}
		usleep(500000);
	}
	Sys_Printf("vkquake: pak0.pak not found after wait (continuing; Host_Init will report)\n");
}

int main(int argc, char *argv[])
{
	double time, oldtime, newtime;

	/* LINE-buffered stdout so each printf line reaches the shared UART console in one
	 * write() (otherwise our log interleaves char-by-char with the concurrent lwip
	 * process). stderr unbuffered so a fault's diagnostics reach the UART immediately. */
	static char vkq_stdout_buf[2048];
	setvbuf(stdout, vkq_stdout_buf, _IOLBF, sizeof(vkq_stdout_buf));
	setvbuf(stderr, NULL, _IONBF, 0);
	printf("vkquake: main() entered (argc=%d)\n", argc);

	host_parms = &parms;
	parms.basedir = "/";    /* absolute: on nfsroot, /id1/pak0.pak is the NFS export */
	parms.userdir = "/";    /* DO_USERDIRS disabled -> userdir == basedir */
	parms.argc = argc;
	parms.argv = argv;
	parms.errstate = 0;

	COM_InitArgv(parms.argc, parms.argv);

	isDedicated = (COM_CheckParm("-dedicated") != 0);

	Sys_Init();

	Sys_Printf("Detected %d CPUs.\n", SDL_GetCPUCount());
	Sys_Printf("Initializing vkQuake (Phoenix/V3DV)\n");

	wait_for_gamedata();
	parms.basedir = g_basedir;      /* whichever path exposed id1/pak0.pak */
	parms.userdir = g_basedir;

	Sys_Printf("Host_Init\n");
	Host_Init();

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
