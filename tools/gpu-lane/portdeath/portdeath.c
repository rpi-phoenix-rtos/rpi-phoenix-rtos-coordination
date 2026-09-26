/*
 * portdeath -- what happens to clients parked in msgSend when their server dies
 *
 * One process plays both sides: it forks the server (so no devfs node and no
 * shell background job is needed -- psh has no '&') and gets the server's port
 * number back over a pipe. N client threads then send one request each and
 * report msgSend's return value and how long it took.
 *
 *   portdeath respond [-n N]  control: the server answers every request (rc=0,
 *                             o.data checked), then exits
 *   portdeath stale           rid reuse: a duplicate msgRespond with a rid that
 *                             was already answered (condition 3)
 *   portdeath queued [-n N]   the server never calls msgRecv, then exits
 *   portdeath exit [-n N]     the server receives all N without answering, then
 *                             exits with a second receiver still in msgRecv
 *   portdeath kill [-n N]     as exit, but the parent SIGKILLs the server
 *
 * Requests alternate between three payload shapes, so every kernel path that
 * maps a payload into the server is covered: raw only (no mapping), a page-
 * aligned 4 KiB in+out buffer (the client's pages mapped into the server), and
 * an unaligned 5000-byte in+out buffer (shadow pages at both ends).
 *
 * Every result line starts with "PORTDEATH". Before kernel branch
 * gpu-lane/port-death, `exit` and `kill` leave the clients parked forever and
 * uninterruptibly: this process prints its verdict and then can never finish
 * exiting, so run those modes LAST in a Pi cycle with a bounded command time.
 *
 * Copyright 2026 Phoenix Systems
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/msg.h>
#include <sys/wait.h>


#define MAX_CLIENTS  16
#define PAGE_SZ      4096
#define UNALIGNED_SZ 5000
#define UNALIGNED_AT 16
#define WAIT_MS      3000 /* how long clients get to return once the server is gone */

enum { MODE_RESPOND, MODE_STALE, MODE_QUEUED, MODE_EXIT, MODE_KILL };
enum { KIND_RAW, KIND_ALIGNED, KIND_UNALIGNED, KIND_COUNT };

static const char *const kind_name[KIND_COUNT] = { "raw", "aligned4k", "unaligned5000" };

typedef struct {
	int idx;
	int kind;
	uint32_t port;
	unsigned char *ibuf, *obuf;
	size_t size;
	volatile int done;
	int rc;
	int oerr;
	int odata_ok;
	int64_t t_start_ms, t_end_ms;
} client_t;

static client_t clients[MAX_CLIENTS];


static int64_t now_ms(void)
{
	struct timespec ts;

	(void)clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}


static unsigned char pattern(size_t i, unsigned int salt)
{
	return (unsigned char)((i * 31u + salt) & 0xffu);
}


/* ------------------------------------------------------------------ server */

static uint32_t srv_port;


static void *srv_idle_receiver(void *arg)
{
	msg_t msg;
	msg_rid_t rid;

	(void)arg;
	/* Blocked in msgRecv when the process dies: models a receiver thread
	 * killed with the rest. Anything it receives is never answered. */
	for (;;) {
		if (msgRecv(srv_port, &msg, &rid) < 0) {
			(void)usleep(1000);
		}
	}
	return NULL;
}


static void srv_fill_reply(msg_t *msg, int oerr)
{
	size_t i;

	if ((msg->o.data != NULL) && (msg->o.size != 0)) {
		for (i = 0; i < msg->o.size; i++) {
			((unsigned char *)msg->o.data)[i] = pattern(i, 0x5a);
		}
	}
	msg->o.err = oerr;
}


/* Runs in the forked child; never returns. */
static void server(int mode, int n, int wfd, int rfd)
{
	msg_t msg[MAX_CLIENTS], m;
	msg_rid_t rid[MAX_CLIENTS], rid_a, rid_b;
	pthread_t th;
	char c;
	int i, rc;

	if (portCreate(&srv_port) < 0) {
		printf("PORTDEATH server FAIL portCreate\n");
		_exit(1);
	}
	if (write(wfd, &srv_port, sizeof(srv_port)) != (ssize_t)sizeof(srv_port)) {
		_exit(1);
	}

	switch (mode) {
		case MODE_QUEUED:
			/* Never receive; die once the parent says its clients are queued */
			(void)read(rfd, &c, 1);
			_exit(0);

		case MODE_STALE:
			/* A: answer at once, remember its rid. B: arrives next, from the
			 * same client. Answer A's rid again, then B properly. */
			if ((msgRecv(srv_port, &m, &rid_a) < 0)) {
				_exit(1);
			}
			m.o.err = 1;
			(void)msgRespond(srv_port, &m, rid_a);
			if ((msgRecv(srv_port, &msg[0], &rid_b) < 0)) {
				_exit(1);
			}
			m.o.err = 99;
			rc = msgRespond(srv_port, &m, rid_a);
			printf("PORTDEATH stale rid_a=%d rid_b=%d dup_respond_rc=%d\n", (int)rid_a, (int)rid_b, rc);
			fflush(stdout);
			(void)usleep(100000);
			msg[0].o.err = 2;
			rc = msgRespond(srv_port, &msg[0], rid_b);
			printf("PORTDEATH stale real_respond_rc=%d\n", rc);
			fflush(stdout);
			_exit(0);

		default:
			break;
	}

	for (i = 0; i < n; i++) {
		if (msgRecv(srv_port, &msg[i], &rid[i]) < 0) {
			printf("PORTDEATH server FAIL msgRecv i=%d\n", i);
			_exit(1);
		}
	}

	if (mode == MODE_RESPOND) {
		for (i = 0; i < n; i++) {
			srv_fill_reply(&msg[i], 7);
			(void)msgRespond(srv_port, &msg[i], rid[i]);
		}
		_exit(0);
	}

	/* exit / kill: hold all N. Scribble on the output windows first, as a
	 * server part-way through a request would. */
	for (i = 0; i < n; i++) {
		srv_fill_reply(&msg[i], 0);
	}
	(void)pthread_create(&th, NULL, srv_idle_receiver, NULL);
	(void)usleep(20000); /* let it block in msgRecv */
	printf("PORTDEATH server holding=%d rid_first=%d rid_last=%d\n", n, (int)rid[0], (int)rid[n - 1]);
	fflush(stdout);
	c = 'A';
	(void)write(wfd, &c, 1);

	if (mode == MODE_EXIT) {
		_exit(0);
	}
	for (;;) {
		(void)pause(); /* MODE_KILL: wait for SIGKILL */
	}
}


/* ------------------------------------------------------------------ client */

static void *client_thread(void *arg)
{
	client_t *cl = arg;
	msg_t msg;
	size_t i;

	memset(&msg, 0, sizeof(msg));
	msg.type = mtDevCtl;
	msg.oid.port = cl->port;
	msg.oid.id = (uint64_t)cl->idx;
	msg.i.raw[0] = (unsigned char)cl->idx;
	if (cl->kind != KIND_RAW) {
		msg.i.data = cl->ibuf;
		msg.i.size = cl->size;
		msg.o.data = cl->obuf;
		msg.o.size = cl->size;
	}

	cl->t_start_ms = now_ms();
	cl->rc = msgSend(cl->port, &msg);
	cl->t_end_ms = now_ms();
	cl->oerr = msg.o.err;

	cl->odata_ok = 1;
	if ((cl->rc == 0) && (cl->kind != KIND_RAW)) {
		for (i = 0; i < cl->size; i++) {
			if (cl->obuf[i] != pattern(i, 0x5a)) {
				cl->odata_ok = 0;
				break;
			}
		}
	}
	__atomic_store_n(&cl->done, 1, __ATOMIC_RELEASE);
	return NULL;
}


static int client_setup(client_t *cl, int idx, uint32_t port)
{
	unsigned char *mem;
	size_t i;

	memset(cl, 0, sizeof(*cl));
	cl->idx = idx;
	cl->kind = idx % KIND_COUNT;
	cl->port = port;

	if (cl->kind == KIND_RAW) {
		return 0;
	}
	/* Two separate 3-page areas: in and out must not share pages */
	mem = mmap(NULL, 6 * PAGE_SZ, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (mem == MAP_FAILED) {
		return -1;
	}
	if (cl->kind == KIND_ALIGNED) {
		cl->ibuf = mem;
		cl->obuf = mem + 3 * PAGE_SZ;
		cl->size = PAGE_SZ;
	}
	else {
		cl->ibuf = mem + UNALIGNED_AT;
		cl->obuf = mem + 3 * PAGE_SZ + UNALIGNED_AT;
		cl->size = UNALIGNED_SZ;
	}
	for (i = 0; i < cl->size; i++) {
		cl->ibuf[i] = pattern(i, 0x11);
		cl->obuf[i] = 0;
	}
	return 0;
}


static int count_done(int n)
{
	int i, k = 0;

	for (i = 0; i < n; i++) {
		k += __atomic_load_n(&clients[i].done, __ATOMIC_ACQUIRE);
	}
	return k;
}


static int run_stale(uint32_t port)
{
	msg_t msg;
	int rc_a, rc_b, err_a;

	memset(&msg, 0, sizeof(msg));
	msg.type = mtDevCtl;
	msg.oid.port = port;
	rc_a = msgSend(port, &msg);
	err_a = msg.o.err;

	memset(&msg, 0, sizeof(msg));
	msg.type = mtDevCtl;
	msg.oid.port = port;
	rc_b = msgSend(port, &msg);

	/* b_err: 2 = B got its own answer; 99 = B got the duplicate meant for A */
	printf("PORTDEATH stale rc_a=%d a_err=%d rc_b=%d b_err=%d verdict=%s\n", rc_a, err_a, rc_b, msg.o.err,
		((rc_b == 0) && (msg.o.err == 2)) ? "PASS" : "FAIL");
	fflush(stdout);
	return ((rc_b == 0) && (msg.o.err == 2)) ? 0 : 1;
}


int main(int argc, char **argv)
{
	int mode, n = 6, i, opt, returned, hung = 0, n_einval = 0, n_zero = 0, n_other = 0, n_odata_bad = 0;
	int tofd[2], fromfd[2], status = 0;
	int64_t t_death, lat, lat_max = 0, t_deadline;
	uint32_t port;
	pthread_t th;
	pid_t pid;
	char c;
	int pass;

	if (argc < 2) {
		fprintf(stderr, "usage: portdeath respond|stale|queued|exit|kill [-n clients]\n");
		return 2;
	}
	if (strcmp(argv[1], "respond") == 0) {
		mode = MODE_RESPOND;
	}
	else if (strcmp(argv[1], "stale") == 0) {
		mode = MODE_STALE;
	}
	else if (strcmp(argv[1], "queued") == 0) {
		mode = MODE_QUEUED;
	}
	else if (strcmp(argv[1], "exit") == 0) {
		mode = MODE_EXIT;
	}
	else if (strcmp(argv[1], "kill") == 0) {
		mode = MODE_KILL;
	}
	else {
		fprintf(stderr, "portdeath: unknown mode '%s'\n", argv[1]);
		return 2;
	}
	optind = 2;
	while ((opt = getopt(argc, argv, "n:")) != -1) {
		if (opt == 'n') {
			n = atoi(optarg);
		}
		else {
			return 2;
		}
	}
	if ((n < 1) || (n > MAX_CLIENTS)) {
		fprintf(stderr, "portdeath: -n must be 1..%d\n", MAX_CLIENTS);
		return 2;
	}

	printf("PORTDEATH start mode=%s n=%d\n", argv[1], n);
	fflush(stdout);

	/* Fork before any thread exists */
	if ((pipe(tofd) < 0) || (pipe(fromfd) < 0)) {
		printf("PORTDEATH FAIL pipe errno=%d\n", errno);
		return 1;
	}
	pid = fork();
	if (pid < 0) {
		printf("PORTDEATH FAIL fork errno=%d\n", errno);
		return 1;
	}
	if (pid == 0) {
		close(tofd[1]);
		close(fromfd[0]);
		server(mode, n, fromfd[1], tofd[0]);
	}
	close(tofd[0]);
	close(fromfd[1]);

	if (read(fromfd[0], &port, sizeof(port)) != (ssize_t)sizeof(port)) {
		printf("PORTDEATH FAIL server did not come up\n");
		return 1;
	}
	printf("PORTDEATH server pid=%d port=%u\n", (int)pid, (unsigned int)port);
	fflush(stdout);

	if (mode == MODE_STALE) {
		i = run_stale(port);
		(void)waitpid(pid, &status, 0);
		return i;
	}

	for (i = 0; i < n; i++) {
		if (client_setup(&clients[i], i, port) < 0) {
			printf("PORTDEATH FAIL client buffers\n");
			return 1;
		}
		if (pthread_create(&th, NULL, client_thread, &clients[i]) != 0) {
			printf("PORTDEATH FAIL pthread_create i=%d\n", i);
			return 1;
		}
		(void)pthread_detach(th);
	}

	if (mode == MODE_QUEUED) {
		(void)usleep(200000); /* every client is queued by now */
		c = 'G';
		(void)write(tofd[1], &c, 1);
	}
	else if ((mode == MODE_EXIT) || (mode == MODE_KILL)) {
		if (read(fromfd[0], &c, 1) != 1) {
			printf("PORTDEATH FAIL server died before holding all requests\n");
		}
		if (mode == MODE_KILL) {
			(void)kill(pid, SIGKILL);
		}
	}

	(void)waitpid(pid, &status, 0);
	t_death = now_ms();
	printf("PORTDEATH server gone exited=%d exit=%d signaled=%d sig=%d\n", WIFEXITED(status) ? 1 : 0,
		WIFEXITED(status) ? WEXITSTATUS(status) : -1, WIFSIGNALED(status) ? 1 : 0,
		WIFSIGNALED(status) ? WTERMSIG(status) : 0);
	fflush(stdout);

	t_deadline = t_death + WAIT_MS;
	while ((count_done(n) < n) && (now_ms() < t_deadline)) {
		(void)usleep(10000);
	}

	returned = 0;
	for (i = 0; i < n; i++) {
		client_t *cl = &clients[i];

		if (__atomic_load_n(&cl->done, __ATOMIC_ACQUIRE) == 0) {
			hung++;
			printf("PORTDEATH client=%d kind=%s HUNG waited_ms=%lld\n", i, kind_name[cl->kind],
				(long long)(now_ms() - cl->t_start_ms));
			continue;
		}
		returned++;
		lat = cl->t_end_ms - t_death;
		if (lat > lat_max) {
			lat_max = lat;
		}
		if (cl->rc == -EINVAL) {
			n_einval++;
		}
		else if (cl->rc == 0) {
			n_zero++;
		}
		else {
			n_other++;
		}
		if (cl->odata_ok == 0) {
			n_odata_bad++;
		}
		printf("PORTDEATH client=%d kind=%s rc=%d o_err=%d odata_ok=%d elapsed_ms=%lld after_death_ms=%lld\n", i,
			kind_name[cl->kind], cl->rc, cl->oerr, cl->odata_ok, (long long)(cl->t_end_ms - cl->t_start_ms),
			(long long)lat);
	}

	if (mode == MODE_RESPOND) {
		pass = (returned == n) && (n_zero == n) && (n_odata_bad == 0);
	}
	else {
		pass = (returned == n) && (n_einval == n) && (lat_max < 1000);
	}
	printf("PORTDEATH result mode=%s n=%d returned=%d hung=%d rc_zero=%d rc_einval=%d rc_other=%d odata_bad=%d "
		"max_after_death_ms=%lld verdict=%s\n",
		argv[1], n, returned, hung, n_zero, n_einval, n_other, n_odata_bad, (long long)lat_max, pass ? "PASS" : "FAIL");
	if (hung != 0) {
		printf("PORTDEATH note: %d client thread(s) still parked; if they are uninterruptible this process "
			"cannot finish exiting\n", hung);
	}
	fflush(stdout);

	_exit(pass ? 0 : 1);
}
