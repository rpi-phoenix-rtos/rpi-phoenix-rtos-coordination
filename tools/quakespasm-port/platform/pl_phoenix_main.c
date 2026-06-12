/*
 * pl_phoenix_main.c — Phoenix entry point + host loop for Quakespasm: replaces
 * main_sdl.c. No SDL init; just COM_InitArgv -> Sys_Init -> Host_Init, then the
 * Host_Frame loop driven by Sys_DoubleTime.
 */
#include "quakedef.h"

#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#define DEFAULT_MEMORY (256 * 1024 * 1024)
/* Quake has large stack frames (model/file buffers) and recursive renderers
 * (R_RecursiveWorldNode, R_SplitEntityOnNode); Phoenix's default main-thread
 * stack is tiny (the engine overflowed it ~132 KiB into Host_Init). Run the host
 * on a generously-sized thread instead. */
#define HOST_STACKSZ (32 * 1024 * 1024)

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

static void *host_thread(void *arg)
{
	double time, oldtime, newtime;
	(void)arg;

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
	return NULL;
}

int main(int argc, char *argv[])
{
	pthread_t th;
	pthread_attr_t attr;

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

	/* Run Host_Init + the frame loop on a large-stack thread (see HOST_STACKSZ). */
	if (pthread_attr_init(&attr) != 0)
		Sys_Error("pthread_attr_init failed");
	if (pthread_attr_setstacksize(&attr, HOST_STACKSZ) != 0)
		Sys_Error("pthread_attr_setstacksize failed");
	if (pthread_create(&th, &attr, host_thread, NULL) != 0)
		Sys_Error("host thread create failed");
	pthread_join(th, NULL);
	return 0;
}
