/*
 * pl_phoenix_stubs.c — small Phoenix shims for symbols vkQuake's excluded platform TUs
 * (net_bsd.c, pl_linux.c) used to provide. First-light: Loopback-only networking (so
 * single-player "map"/"newgame" via Loop_Connect works; no UDP/LAN — see the quakespasm
 * port's rationale below), and the Mesa clock helper libphoenix lacks.
 *
 * Adapted from quakespasm-port/platform/pl_phoenix_stubs.c. vkQuake's net_driver_t has a
 * different field set than quakespasm's (it adds QueryAddresses + QGetAnyMessage and
 * renames Get/SendMessage to QGet/QSendMessage), so we use DESIGNATED initializers keyed
 * to net_defs.h's field names — robust to the layout change.
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
 * The Datagram + UDP LAN driver is intentionally NOT registered: enabling it makes
 * Quake's per-frame net poll service incoming UDP, and on the BCM2711 LAN + Phoenix lwIP
 * that path hit FIONREAD/ENOSYS faults and regressed NFS/rendering in the GL port. LAN
 * multiplayer needs a careful combined FIONREAD + NFS-safe fix (deferred). Loopback-only
 * keeps single-player solid. Loop_* live in net_loop.c (in the engine build). */
net_driver_t net_drivers[] = {
	{
		.name = "Loopback",
		.initialized = false,
		.Init = Loop_Init,
		.Listen = Loop_Listen,
		.QueryAddresses = Loop_QueryAddresses,     /* NULL macro in net_loop.h */
		.SearchForHosts = Loop_SearchForHosts,
		.Connect = Loop_Connect,
		.CheckNewConnections = Loop_CheckNewConnections,
		.QGetAnyMessage = Loop_GetAnyMessage,
		.QGetMessage = Loop_GetMessage,
		.QSendMessage = Loop_SendMessage,
		.SendUnreliableMessage = Loop_SendUnreliableMessage,
		.CanSendMessage = Loop_CanSendMessage,
		.CanSendUnreliableMessage = Loop_CanSendUnreliableMessage,
		.Close = Loop_Close,
		.Shutdown = Loop_Shutdown,
	},
};
const int net_numdrivers = countof(net_drivers);

/* No LAN drivers (UDP/BSD sockets excluded — see above). */
net_landriver_t net_landrivers[1];
const int       net_numlandrivers = 0;

/* --- Mesa clock helper libphoenix lacks (referenced by Mesa's thread utils) --- */
int pthread_getcpuclockid(pthread_t thread, clockid_t *clock_id)
{
	(void)thread;
	if (clock_id != NULL)
		*clock_id = CLOCK_MONOTONIC;
	return 0;
}
