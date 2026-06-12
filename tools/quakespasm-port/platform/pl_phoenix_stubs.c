/*
 * pl_phoenix_stubs.c — small Phoenix shims for symbols Quakespasm's excluded
 * platform TUs (net_bsd.c, sys_sdl_unix.c, pl_*.c) used to provide. First-light:
 * no networking (empty driver tables -> single-player local play still works via
 * the loopback path registered by net_main when present; expand if a demo needs
 * net_loop), no clipboard, no console input.
 */
#include "quakedef.h"
typedef int sys_socket_t;       /* normally from net_sys.h (platform-gated) */
#include "net_defs.h"

#include <time.h>
#include <pthread.h>

/* --- network driver tables (were in net_bsd.c). Empty for first-light. --- */
net_driver_t   net_drivers[1];
const int      net_numdrivers = 0;
net_landriver_t net_landrivers[1];
const int      net_numlandrivers = 0;

/* --- throttle cvar (was in main_sdl.c; sys_ticrate is owned by host.c) --- */
cvar_t sys_throttle = { "sys_throttle", "0.02", CVAR_ARCHIVE };

/* --- platform odds & ends --- */
char *PL_GetClipboardData(void)
{
	return NULL;
}

const char *Sys_ConsoleInput(void)
{
	return NULL;
}

/* Phoenix libc lacks pthread_getcpuclockid (referenced by Mesa's thread utils);
 * a monotonic-clock stand-in is fine for Mesa's timing. */
int pthread_getcpuclockid(pthread_t thread, clockid_t *clock_id)
{
	(void)thread;
	if (clock_id)
		*clock_id = CLOCK_MONOTONIC;
	return 0;
}
