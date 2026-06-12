/*
 * pl_phoenix_main.c — Phoenix entry point + host loop for Quakespasm: replaces
 * main_sdl.c. No SDL init; just COM_InitArgv -> Sys_Init -> Host_Init, then the
 * Host_Frame loop driven by Sys_DoubleTime.
 */
#include "quakedef.h"

#include <stdlib.h>
#include <unistd.h>

#define DEFAULT_MEMORY (256 * 1024 * 1024)

static quakeparms_t parms;      /* host_parms (the pointer) is owned by host.c */

int main(int argc, char *argv[])
{
	double time, oldtime, newtime;

	/* Unbuffered stdout so prints reach the UART immediately (psh stdout may be
	 * fully buffered -> buffered output is lost if the process exits/faults early). */
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);
	printf("quakespasm: main() entered (argc=%d)\n", argc);

	host_parms = &parms;
	parms.basedir = ".";
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
