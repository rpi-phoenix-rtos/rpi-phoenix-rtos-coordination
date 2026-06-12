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

/* Wait (bounded) for the game data to be reachable before Host_Init. This handles
 * two Phoenix realities on the nfsroot variant: (1) the NFS root is mounted by the
 * userspace nfs-fs takeover slightly after plo boot-launches us, and (2) the libnfs
 * dircache (#156) ENOENTs a file on first access but a retry succeeds. Polling
 * fopen() covers both. On netboot (no data) this simply times out, then Host_Init
 * prints the normal "needs pak0.pak" message. */
static void wait_for_gamedata(void)
{
	const char *pak = "/id1/pak0.pak";
	int i;
	for (i = 0; i < 60; i++) {     /* ~30 s */
		FILE *f = fopen(pak, "rb");
		if (f) {
			fclose(f);
			Sys_Printf("quakespasm: found %s after %d tries\n", pak, i + 1);
			return;
		}
		usleep(500000);
	}
	Sys_Printf("quakespasm: %s not found after wait (continuing; Host_Init will report)\n", pak);
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
