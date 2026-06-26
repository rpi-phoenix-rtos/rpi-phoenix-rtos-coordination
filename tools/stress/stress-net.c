/*
 * Phoenix-RTOS Raspberry Pi 4 — network stress: TCP echo server (Pi side).
 *
 * A tiny, bounded BSD-socket echo server used as a CONTROLLED Pi-as-server
 * target for the host-side echo load generator (tools/stress/net/echo-load.py).
 * It faithfully echoes every byte it receives; the host verifies the echo and
 * flags any mismatch as FAULT (corruption). The server itself only classifies
 * its own socket-layer outcomes per the three-bucket model (see stress.h):
 *   OK     accept/recv/send completed as intended.
 *   LIMIT  the stack correctly refused at a boundary (EMFILE/ENFILE/ENOMEM/
 *          EAGAIN) — correct behaviour, not a defect.
 *   FAULT  a socket call failed with an errno that is not a legitimate limit.
 *
 * Lifecycle: psh has no job control, so the server is BOUNDED — it runs for a
 * fixed wall-clock duration (default 60 s) OR until it has handled a fixed
 * number of connections, whichever comes first, then prints STRESS-SUMMARY and
 * exits cleanly. Heartbeats (STRESS-HB) pin a hang to the exact op.
 *
 * Usage (psh, from the NFS root):
 *   /bin/stress-net [port] [duration_s] [max_conns]
 * Defaults: port 7777, duration 60 s, max_conns 0 (unlimited within duration).
 *
 * Build + stage: tools/stress/net/build-stress-net.sh (host-side, no Pi boot).
 *
 * Copyright 2026 Phoenix Systems
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "stress.h"

#define SUITE "net"

#define ECHO_BUFSZ 4096

/* Monotonic-ish seconds. Phoenix supports CLOCK_MONOTONIC; fall back to time(). */
static long now_s(void)
{
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
		return (long)ts.tv_sec;
	}
	return (long)time(NULL);
}

/* Serve one accepted connection: echo until the peer closes (recv==0) or a
 * socket error. Returns bytes echoed (>=0), or -1 on a send/recv FAULT. */
static long serve_conn(sr_tally_t *t, int cfd)
{
	char buf[ECHO_BUFSZ];
	long total = 0;

	for (;;) {
		ssize_t n = recv(cfd, buf, sizeof(buf), 0);
		if (n == 0) {
			/* Orderly peer close — connection done. */
			return total;
		}
		if (n < 0) {
			if (errno == EINTR) {
				continue;
			}
			if (sr_errno_is_limit(errno)) {
				SR_LIMIT(t, SUITE, "recv", "errno=%d (%s)", errno, strerror(errno));
			}
			else {
				SR_FAULT(t, SUITE, "recv", "errno=%d (%s) after %ld bytes", errno, strerror(errno), total);
			}
			return -1;
		}

		/* Echo the whole chunk back, handling partial sends. */
		ssize_t off = 0;
		while (off < n) {
			ssize_t w = send(cfd, buf + off, (size_t)(n - off), 0);
			if (w < 0) {
				if (errno == EINTR) {
					continue;
				}
				if (sr_errno_is_limit(errno)) {
					SR_LIMIT(t, SUITE, "send", "errno=%d (%s)", errno, strerror(errno));
				}
				else {
					SR_FAULT(t, SUITE, "send", "errno=%d (%s) after %ld bytes", errno, strerror(errno), total);
				}
				return -1;
			}
			off += w;
			total += w;
		}
	}
}

int main(int argc, char **argv)
{
	sr_tally_t tally = { 0 };
	int port = 7777;
	long duration = 60;
	long max_conns = 0; /* 0 = unlimited within the duration window */

	sr_init();

	if (argc > 1) {
		port = atoi(argv[1]);
	}
	if (argc > 2) {
		duration = atol(argv[2]);
	}
	if (argc > 3) {
		max_conns = atol(argv[3]);
	}

	SR_HB(SUITE, "start port=%d duration=%lds max_conns=%ld", port, duration, max_conns);

	int sfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sfd < 0) {
		SR_FAULT(&tally, SUITE, "socket", "errno=%d (%s)", errno, strerror(errno));
		SR_SUMMARY(&tally, SUITE, "conns=0");
		return 1;
	}

	int one = 1;
	/* SO_REUSEADDR so a re-run right after exit isn't blocked by TIME_WAIT. */
	(void)setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons((unsigned short)port);

	if (bind(sfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		SR_FAULT(&tally, SUITE, "bind", "port=%d errno=%d (%s)", port, errno, strerror(errno));
		close(sfd);
		SR_SUMMARY(&tally, SUITE, "conns=0");
		return 1;
	}

	if (listen(sfd, 16) < 0) {
		SR_FAULT(&tally, SUITE, "listen", "errno=%d (%s)", errno, strerror(errno));
		close(sfd);
		SR_SUMMARY(&tally, SUITE, "conns=0");
		return 1;
	}

	SR_OK(&tally, SUITE, "listen", "port=%d backlog=16", port);

	long deadline = now_s() + duration;
	long conns = 0;
	long bytes = 0;

	while (now_s() < deadline) {
		if (max_conns > 0 && conns >= max_conns) {
			break;
		}

		struct sockaddr_in peer;
		socklen_t plen = sizeof(peer);
		int cfd = accept(sfd, (struct sockaddr *)&peer, &plen);
		if (cfd < 0) {
			if (errno == EINTR) {
				continue;
			}
			if (sr_errno_is_limit(errno)) {
				/* Stack correctly refused (fd cap / mem) — back off briefly. */
				SR_LIMIT(&tally, SUITE, "accept", "errno=%d (%s)", errno, strerror(errno));
				usleep(10000);
				continue;
			}
			SR_FAULT(&tally, SUITE, "accept", "errno=%d (%s)", errno, strerror(errno));
			break;
		}

		conns++;
		SR_HB(SUITE, "accepted conn=%ld from=%s:%u", conns,
			inet_ntoa(peer.sin_addr), (unsigned)ntohs(peer.sin_port));

		long echoed = serve_conn(&tally, cfd);
		close(cfd);

		if (echoed < 0) {
			/* serve_conn already emitted LIMIT/FAULT; keep serving others. */
			continue;
		}
		bytes += echoed;
		SR_OK(&tally, SUITE, "echo", "conn=%ld bytes=%ld", conns, echoed);
	}

	close(sfd);
	SR_HB(SUITE, "shutdown conns=%ld bytes=%ld", conns, bytes);
	SR_SUMMARY(&tally, SUITE, "conns=%ld bytes=%ld", conns, bytes);

	return tally.fault > 0 ? 1 : 0;
}
