/*
 * udprtt -- raw UDP round-trip latency from Phoenix to a host echo server.
 *
 * The NFS per-lookup cost is ~5 ms per RPC and is NOT poll-timeout-bound.  This
 * separates the two remaining suspects: if a bare UDP ping-pong also costs ~5 ms
 * the lwip stack / scheduling is responsible (and every network app pays it);
 * if it is ~0.3 ms the cost is inside nfs-fs / libnfs.
 *
 * Usage: udprtt <host-ip> <port> [count]
 *
 * Copyright 2026 Phoenix Systems  %LICENSE%
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static long long now_us(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);

	return ((long long)ts.tv_sec * 1000000LL) + (ts.tv_nsec / 1000);
}

int main(int argc, char **argv)
{
	const char *ip = (argc > 1) ? argv[1] : "10.42.0.1";
	int port = (argc > 2) ? atoi(argv[2]) : 9998;
	int count = (argc > 3) ? atoi(argv[3]) : 50;
	struct sockaddr_in sa;
	char buf[64];
	long long tot = 0, worst = 0, best = 1000000000LL;
	int fd, i, ok = 0;

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		fprintf(stderr, "udprtt: socket failed\n");
		return 1;
	}
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons((unsigned short)port);
	sa.sin_addr.s_addr = inet_addr(ip);

	for (i = 0; i < count; i++) {
		long long t0 = now_us(), dt;
		ssize_t r;

		if (sendto(fd, "p", 1, 0, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
			continue;
		}
		r = recv(fd, buf, sizeof(buf), 0);
		dt = now_us() - t0;
		if (r > 0) {
			ok++;
			tot += dt;
			if (dt > worst) {
				worst = dt;
			}
			if (dt < best) {
				best = dt;
			}
		}
	}
	close(fd);

	printf("UDPRTT ok=%d/%d avg=%lldus best=%lldus worst=%lldus\n", ok, count,
			(ok > 0) ? (tot / ok) : 0, (ok > 0) ? best : 0, worst);

	return 0;
}
