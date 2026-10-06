/*
 * Host regression test: a libnfs sync call that gives up at its overall
 * deadline (ports/libnfs/patches/04-sync-call-overall-deadline.patch) must not
 * leave any PDU queued that still points at the caller's stack.
 *
 * The bug (Pi, build 39): the "nfs" server's loop thread took a Data Abort right
 * after an NFSv4 reclaim. A 54 MB nfs_pwrite() hit the deadline while libnfs was
 * between reconnect attempts (is_connected == 0); rpc_disconnect() returns early
 * in that state without failing the queued PDUs, so the WRITE PDU survived with
 * private_data -> nfs_pwrite()'s on-stack sync_cb_data. nfs_reclaim() then
 * called nfs_destroy_context() on the old context, which cancelled that PDU, and
 * pwrite_cb() stored {is_finished = 1, status = -EINTR} -- 0xfffffffc00000001 --
 * over the slot where nfs_reclaim() had saved the caller's x21.
 *
 * This harness builds exactly that state on the host, against a fake server:
 *   1. connect a real libnfs context to a local listener (auto-reconnect on,
 *      retrans = 2, as nfs_mount() configures it on the Pi);
 *   2. the server reads the request and never answers; it fills its own accept
 *      queue (so the client's reconnect SYN is dropped: is_connected stays 0)
 *      and closes the connection, which makes libnfs requeue and reconnect;
 *   3. the sync call returns at the deadline;
 *   4. CHECK A: no PDU may remain anywhere in the context;
 *   5. CHECK B: destroy the context from a frame filled with a canary that
 *      overlaps the dead sync frame; the canary must survive;
 *   6. CHECK C: the abandoned context must hold no socket (mid-reconnect,
 *      libnfs parks the old one in old_fd and nothing ever closes it).
 * It runs the shape twice: nfs_renew() (the lease keepalive) and nfs_pwrite()
 * of a 4 MB buffer (the crash).
 *
 * Prints "LIBNFS-STALE-PDU: PASS" or "LIBNFS-STALE-PDU: FAIL <why>".
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "config.h"
#include "libnfs-zdr.h"
#include "libnfs.h"
#include "libnfs-raw.h"
#include "libnfs-raw-mount.h"
#include "libnfs-raw-nfs.h"
#include "libnfs-raw-nfs4.h"
#include "libnfs-private.h" /* the queues CHECK A inspects */


#define PDU_TIMEOUT_MS 400 /* per-PDU timeout; the sync deadline is 4x this */
#define CANARY_WORDS   2048
#define CANARY         0x5a5a5a5a5a5a5a5aULL


struct fakesrv {
	int lfd;
	int port;
	int dummies[4];
	pthread_t thr;
};


static int dialNonblock(int port)
{
	struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(port) };
	int fd = socket(AF_INET, SOCK_STREAM, 0);

	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
	(void)connect(fd, (struct sockaddr *)&a, sizeof(a));
	return fd;
}


/* Take one connection, swallow the request, never answer. Then make sure a
 * reconnect cannot complete -- fill the accept queue, which makes Linux drop
 * further SYNs -- and hang up, so the client is left reconnecting. */
static void *fakesrvThread(void *arg)
{
	struct fakesrv *s = arg;
	char buf[65536];
	int c = accept(s->lfd, NULL, NULL);

	if (c < 0) {
		return NULL;
	}
	if (read(c, buf, sizeof(buf)) <= 0) {
		close(c);
		return NULL;
	}

	for (int i = 0; i < 4; i++) {
		s->dummies[i] = dialNonblock(s->port);
	}
	usleep(100000);
	close(c);
	return NULL;
}


static int fakesrvStart(struct fakesrv *s)
{
	struct sockaddr_in a = { .sin_family = AF_INET };
	socklen_t al = sizeof(a);

	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	s->lfd = socket(AF_INET, SOCK_STREAM, 0);
	if ((s->lfd < 0) || (bind(s->lfd, (struct sockaddr *)&a, sizeof(a)) != 0) ||
		(listen(s->lfd, 0) != 0) || (getsockname(s->lfd, (struct sockaddr *)&a, &al) != 0)) {
		return -1;
	}
	s->port = ntohs(a.sin_port);
	return pthread_create(&s->thr, NULL, fakesrvThread, s);
}


static void fakesrvStop(struct fakesrv *s)
{
	pthread_join(s->thr, NULL);
	for (int i = 0; i < 4; i++) {
		close(s->dummies[i]);
	}
	close(s->lfd);
}


static void connectCb(struct rpc_context *rpc, int status, void *data, void *priv)
{
	(void)rpc;
	(void)data;
	*(int *)priv = (status == RPC_STATUS_SUCCESS) ? 1 : -1;
}


static struct nfs_context *connectClient(int port)
{
	struct nfs_context *nfs = nfs_init_context();
	struct rpc_context *rpc;
	int done = 0;

	if (nfs == NULL) {
		return NULL;
	}
	nfs_set_version(nfs, NFS_V4);
	nfs_set_poll_timeout(nfs, 1); /* as nfs_makeContext() on the Pi */
	rpc = nfs_get_rpc_context(nfs);
	if (rpc_connect_async(rpc, "127.0.0.1", port, connectCb, &done) != 0) {
		nfs_destroy_context(nfs);
		return NULL;
	}
	while (done == 0) {
		struct pollfd pfd = { .fd = rpc_get_fd(rpc), .events = rpc_which_events(rpc) };

		(void)poll(&pfd, 1, 100);
		if (rpc_service(rpc, pfd.revents) < 0) {
			break;
		}
	}
	if (done != 1) {
		nfs_destroy_context(nfs);
		return NULL;
	}
	/* What nfs_mount() sets once mounted: reconnect forever, retransmit (hard
	 * mount). Retrans > 0 is what keeps a timed-out PDU queued. */
	rpc_set_resiliency(rpc, -1, PDU_TIMEOUT_MS, 2);
	return nfs;
}


/* Every PDU the context still owns, wherever it is. */
static int countPdus(struct rpc_context *rpc)
{
	int n = (rpc->pdu != NULL) ? 1 : 0;

	for (struct rpc_pdu *p = rpc->outqueue.head; p != NULL; p = p->next) {
		n++;
	}
	for (unsigned int i = 0; i < (unsigned int)rpc->num_hashes; i++) {
		for (struct rpc_pdu *p = rpc->waitpdu[i].head; p != NULL; p = p->next) {
			n++;
		}
	}
	return n;
}


/* Destroy the context from a frame that overlaps the dead sync-call frame, and
 * report how many canary words a late completion overwrote. */
static __attribute__((noinline)) int destroyUnderCanary(struct nfs_context *nfs)
{
	volatile uint64_t canary[CANARY_WORDS];
	int hit = 0;

	for (int i = 0; i < CANARY_WORDS; i++) {
		canary[i] = CANARY;
	}
	nfs_destroy_context(nfs);
	for (int i = 0; i < CANARY_WORDS; i++) {
		if (canary[i] != CANARY) {
			printf("  canary word %d overwritten: 0x%016llx\n", i, (unsigned long long)canary[i]);
			hit++;
		}
	}
	return hit;
}


static uint64_t nowMs(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}


enum { CALL_RENEW, CALL_PWRITE };


static __attribute__((noinline)) int doCall(struct nfs_context *nfs, int which, void *buf, size_t len)
{
	if (which == CALL_RENEW) {
		return nfs_renew(nfs);
	}

	/* Any filehandle will do: the server never answers. */
	static char fhval[8];
	static struct nfsfh fh;

	fh.fh.len = sizeof(fhval);
	fh.fh.val = fhval;
	return nfs_pwrite(nfs, &fh, buf, len, 0);
}


static int runCase(const char *name, int which)
{
	struct fakesrv s;
	struct nfs_context *nfs;
	size_t len = 4u << 20;
	void *buf = calloc(1, len);
	int fails = 0;

	if ((buf == NULL) || (fakesrvStart(&s) != 0)) {
		printf("  %s: harness setup failed\n", name);
		return 1;
	}
	nfs = connectClient(s.port);
	if (nfs == NULL) {
		printf("  %s: could not connect to the fake server\n", name);
		return 1;
	}

	uint64_t t0 = nowMs();
	int rc = doCall(nfs, which, buf, len);
	uint64_t dt = nowMs() - t0;
	int left = countPdus(nfs_get_rpc_context(nfs));
	int fd = nfs_get_fd(nfs);
	int hit = destroyUnderCanary(nfs);

	printf("  %s: rc=%d after %llu ms, PDUs left queued=%d, canary words hit=%d, fd after=%d\n",
		name, rc, (unsigned long long)dt, left, hit, fd);
	if (rc != -ETIMEDOUT) {
		printf("  %s: expected -ETIMEDOUT (%d) -- the deadline path was not exercised\n", name, -ETIMEDOUT);
		fails++;
	}
	if (left != 0) {
		fails++;
	}
	if (hit != 0) {
		fails++;
	}
	if (fd != -1) {
		printf("  %s: the abandoned context still holds a socket (leaked on every timeout)\n", name);
		fails++;
	}

	fakesrvStop(&s);
	free(buf);
	return fails;
}


int main(void)
{
	int fails = 0;

	fails += runCase("renew", CALL_RENEW);
	fails += runCase("pwrite-4MB", CALL_PWRITE);

	if (fails == 0) {
		printf("LIBNFS-STALE-PDU: PASS\n");
		return 0;
	}
	printf("LIBNFS-STALE-PDU: FAIL (%d check(s); a sync call returned at its deadline with a PDU still pointing at its stack frame)\n", fails);
	return 1;
}
