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
#include "net_dgrm.h"
#include "net_udp.h"

#include <time.h>
#include <pthread.h>

/* --- network driver tables (were in net_bsd.c, which this port excludes). ---
 * These four arrays are the ENTIRE content of net_bsd.c (just registration tables, no
 * socket "glue"), so the port owns them here and keeps net_bsd.c excluded from the build
 * (avoids duplicate symbols). Register BOTH:
 *  - Loopback: single-player "map"/"newgame" (CL_EstablishConnection("local") -> Loop_Connect).
 *  - Datagram + UDP LAN: multiplayer. net_dgrm.c/net_udp.c already compile+link (only net_bsd
 *    is excluded), and every socket primitive UDP_* needs (socket/bind/sendto/recvfrom/
 *    setsockopt(SO_BROADCAST)/ioctl(FIONBIO/FIONREAD)/inet_addr/htonl…) is real in libphoenix/
 *    lwip. Registering the UDP landriver is what sets tcpipAvailable=true, so the multiplayer
 *    menu works and a listen server + connect-by-IP function. (gethostbyname is a libphoenix
 *    NULL stub -> self-IP falls back to loopback, so broadcast `slist` discovery needs the
 *    Phase-1 SIOCGIFADDR fix; connect-by-numeric-IP does NOT use it and works now.)
 * Loop_* live in net_loop.c, Datagram_* in net_dgrm.c, UDP_* in net_udp.c — all in CORE. */
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
	},
	{	"Datagram",
		false,
		Datagram_Init,
		Datagram_Listen,
		Datagram_SearchForHosts,
		Datagram_Connect,
		Datagram_CheckNewConnections,
		Datagram_GetMessage,
		Datagram_SendMessage,
		Datagram_SendUnreliableMessage,
		Datagram_CanSendMessage,
		Datagram_CanSendUnreliableMessage,
		Datagram_Close,
		Datagram_Shutdown
	}
};
const int      net_numdrivers = Q_COUNTOF(net_drivers);

net_landriver_t net_landrivers[] =
{
	{	"UDP",
		false,
		0,
		UDP_Init,
		UDP_Shutdown,
		UDP_Listen,
		UDP_OpenSocket,
		UDP_CloseSocket,
		UDP_Connect,
		UDP_CheckNewConnections,
		UDP_Read,
		UDP_Write,
		UDP_Broadcast,
		UDP_AddrToString,
		UDP_StringToAddr,
		UDP_GetSocketAddr,
		UDP_GetNameFromAddr,
		UDP_GetAddrFromName,
		UDP_AddrCompare,
		UDP_GetSocketPort,
		UDP_SetSocketPort
	}
};
const int      net_numlandrivers = Q_COUNTOF(net_landrivers);

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
