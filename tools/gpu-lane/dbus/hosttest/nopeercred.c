/*
 * nopeercred.so -- LD_PRELOAD for the HOST dbus-daemon in hosttest/run.sh: makes the
 * peer-credential socket options fail with ENOPROTOOPT, as they do on Phoenix-RTOS today
 * (posix/usocket.c knows SO_RCVBUF and SO_ERROR only). The daemon then learns no peer
 * uid/pid, exactly as the Phoenix build, which compiles no credentials path at all.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

int getsockopt(int fd, int level, int name, void *val, socklen_t *len)
{
	static int (*real)(int, int, int, void *, socklen_t *);

	if (level == SOL_SOCKET) {
		switch (name) {
			case SO_PEERCRED:
#ifdef SO_PEERSEC
			case SO_PEERSEC:
#endif
#ifdef SO_PEERPIDFD
			case SO_PEERPIDFD:
#endif
#ifdef SO_PEERGROUPS
			case SO_PEERGROUPS:
#endif
				if (getenv("NOPEERCRED_QUIET") == NULL) {
					static const char msg[] = "nopeercred: refused a peer-credential getsockopt\n";
					(void)!write(2, msg, sizeof(msg) - 1);
				}
				errno = ENOPROTOOPT;
				return -1;
			default:
				break;
		}
	}
	if (real == NULL) {
		real = (int (*)(int, int, int, void *, socklen_t *))dlsym(RTLD_NEXT, "getsockopt");
	}
	return real(fd, level, name, val, len);
}
