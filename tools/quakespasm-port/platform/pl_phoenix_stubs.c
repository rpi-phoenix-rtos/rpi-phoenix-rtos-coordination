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
#include "net_loop.h"

#include <time.h>
#include <pthread.h>

/* --- network driver tables (were in net_bsd.c, which this port excludes). ---
 * Register ONLY the Loopback driver (single-player "map"/"newgame" via Loop_Connect).
 *
 * The Datagram + UDP LAN driver was REVERTED (it broke the single-player flagship): registering
 * the UDP landriver makes Quake's per-frame net poll service incoming UDP, and the BCM2711 LAN
 * has stray UDP/broadcast traffic — when a packet arrives, UDP_CheckNewConnections calls
 * ioctl(FIONREAD), which on Phoenix lwIP errors ENOSYS -> Quake Error (fatal hang at the demo).
 * Enabling lwIP FIONREAD (LWIP_FIONREAD_LINUXMODE) made the server start but ALSO regressed
 * NFS/rendering (the demo never rendered). So LAN multiplayer needs BOTH a working FIONREAD AND
 * a fix that doesn't break the NFS path — a careful combined effort, deferred (task #26). Until
 * then, Loopback-only keeps single-player solid. Loop_* live in net_loop.c (CORE). */
net_driver_t net_drivers[] =
{
	{	"Loopback",
		false,
		Loop_Init,
		Loop_Listen,
		Loop_SearchForHosts,
		Loop_Connect,
		Loop_CheckNewConnections,
		Loop_GetMessage,
		Loop_SendMessage,
		Loop_SendUnreliableMessage,
		Loop_CanSendMessage,
		Loop_CanSendUnreliableMessage,
		Loop_Close,
		Loop_Shutdown
	}
};
const int      net_numdrivers = Q_COUNTOF(net_drivers);
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
