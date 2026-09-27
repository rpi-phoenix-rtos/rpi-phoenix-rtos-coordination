/*
 * pollwake -- poll() wake-up latency on server-backed descriptors
 *
 * Test for the kernel's pollNotify readiness wake-up (phoenix-rtos-kernel
 * branch gpu-lane/poll-wake). Design, predictions and the Pi run:
 * docs/gpu-new-lane/poll-wake.md.
 *
 *   pollwake server [-f] [-p period_us] [-j jitter_us]
 *       Detaches itself (psh has no "&"; -f stays in the foreground) and
 *       returns once both devices are registered:
 *         /dev/pollwake         calls pollNotify() for every event it queues
 *         /dev/pollwake-legacy  never calls it (an unmodified server)
 *       An event thread queues one event per open file on BOTH devices every
 *       period_us +- jitter_us (default 16667 +- 8000: a 60 Hz source whose
 *       phase drifts against the kernel's 20 ms re-poll, so the legacy latency
 *       spans the whole 0-20 ms window instead of locking to one value).
 *       poll() reports POLLIN while a file's queue is non-empty; read() is
 *       non-blocking and returns the queued events.
 *
 *   pollwake client <test>
 *       single pipe unix pipewake unixwake timeout eintr all quit
 *
 * Every result is one line starting with "POLLWAKE <test>".
 *
 * Build: tools/gpu-lane/pollwake/build.sh (standalone, tree sysroot).
 *
 * Copyright 2026 Phoenix Systems
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/msg.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/threads.h>
#include <phoenix/file.h>
#include <posix/utils.h>

/* The kernel call under test. Declared here as well, so this file builds
 * against a libphoenix whose <sys/msg.h> predates it (build.sh then links
 * pollnotify_shim.S, which supplies the syscall stub). */
extern int pollNotify(const oid_t *oid);

#define DEV_NOTIFY "/dev/pollwake"
#define DEV_LEGACY "/dev/pollwake-legacy"
#define MAX_FILES  32
#define EVQ_LEN    16
#define STACK_SZ   (32u * 1024u)
#define N_EVENTS   60
#define REQ_MAGIC  0x50574b31u /* "PWK1" */

enum { DEV_N = 0, DEV_L = 1, NDEV = 2 };

enum { OP_STATS = 1, OP_QUIT };

typedef struct {
	uint64_t seq;
	uint64_t t_event; /* cntvct when the server queued it */
} pw_event_t;

typedef struct {
	uint32_t magic;
	uint32_t op;
} req_t;

/* Per device; returned by OP_STATS in o.raw. */
typedef struct {
	uint64_t pollstatus_msgs;
	uint64_t pollstatus_ready;
	uint64_t events;
	uint64_t notifies;
	int64_t notify_last_err;
	uint64_t notify_errs;
	uint64_t dropped;
	int64_t notify_supported;
} stats_t;

_Static_assert(sizeof(req_t) <= 64, "req_t must fit msg.i.raw");
_Static_assert(sizeof(stats_t) <= 64, "stats_t must fit msg.o.raw");
_Static_assert(sizeof(pw_event_t) * 4u <= 64, "four events fit a packed read");


/* ------------------------------------------------------------------ time */

static uint64_t cnt_freq;

static inline uint64_t now_cnt(void)
{
	uint64_t v;
	__asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(v) : : "memory");
	return v;
}

static void time_init(void)
{
	uint64_t f;
	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(f));
	cnt_freq = (f != 0) ? f : 54000000u;
}

static inline double cnt_to_us(uint64_t c)
{
	return ((double)c * 1000000.0) / (double)cnt_freq;
}

static uint32_t rng_state = 0x2545f491u;

static uint32_t rng(void)
{
	uint32_t x = rng_state;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	rng_state = x;
	return x;
}


/* ------------------------------------------------------------ server state */

typedef struct {
	int used;
	int dev;
	unsigned int head, tail;
	pw_event_t q[EVQ_LEN];
} file_t;

static struct {
	uint32_t port[NDEV];
	handle_t lock;
	handle_t qcond;
	file_t files[MAX_FILES]; /* index = oid.id, shared id space across both ports */
	stats_t st[NDEV];
	uint64_t seq;
	uint32_t period_us, jitter_us;
	int notify_ok;
	volatile int quit;
} srv;


static int file_alloc(int dev)
{
	int i;

	for (i = 1; i < MAX_FILES; i++) {
		if (srv.files[i].used == 0) {
			memset(&srv.files[i], 0, sizeof(srv.files[i]));
			srv.files[i].used = 1;
			srv.files[i].dev = dev;
			return i;
		}
	}
	return -ENFILE;
}


/* The event source. For the opted-in device the order is the contract every
 * server must keep: make the state an atPollStatus answer reads ready FIRST
 * (under the lock), THEN pollNotify(). */
static void event_thread(void *arg)
{
	oid_t notify[MAX_FILES];
	int i, n, err;
	uint32_t span;

	(void)arg;
	for (;;) {
		span = (srv.jitter_us != 0) ? (rng() % (2u * srv.jitter_us + 1u)) : 0u;
		(void)usleep(srv.period_us - srv.jitter_us + span);

		n = 0;
		mutexLock(srv.lock);
		if (srv.quit != 0) {
			mutexUnlock(srv.lock);
			break;
		}
		srv.seq++;
		for (i = 1; i < MAX_FILES; i++) {
			file_t *f = &srv.files[i];
			if (f->used == 0) {
				continue;
			}
			if ((f->tail - f->head) >= EVQ_LEN) {
				srv.st[f->dev].dropped++;
				continue;
			}
			f->q[f->tail % EVQ_LEN].seq = srv.seq;
			f->q[f->tail % EVQ_LEN].t_event = now_cnt();
			f->tail++;
			srv.st[f->dev].events++;
			if ((f->dev == DEV_N) && (srv.notify_ok != 0)) {
				notify[n].port = srv.port[DEV_N];
				notify[n].id = (id_t)i;
				n++;
			}
		}
		mutexUnlock(srv.lock);

		for (i = 0; i < n; i++) {
			err = pollNotify(&notify[i]);
			mutexLock(srv.lock);
			srv.st[DEV_N].notifies++;
			if (err < 0) {
				srv.st[DEV_N].notify_errs++;
				srv.st[DEV_N].notify_last_err = err;
			}
			mutexUnlock(srv.lock);
		}
	}
	endthread();
}


static void handle(msg_t *msg, int dev)
{
	int id = (int)msg->oid.id;
	file_t *f;

	mutexLock(srv.lock);
	f = ((id > 0) && (id < MAX_FILES) && (srv.files[id].used != 0) && (srv.files[id].dev == dev)) ? &srv.files[id] : NULL;

	switch (msg->type) {
		case mtOpen:
			msg->o.err = file_alloc(dev); /* > 0: the kernel makes it this fd's oid.id */
			break;

		case mtClose:
			if (f != NULL) {
				f->used = 0;
			}
			msg->o.err = EOK;
			break;

		case mtRead: {
			size_t done = 0;
			if (f == NULL) {
				msg->o.err = -EBADF;
				break;
			}
			while ((f->head != f->tail) && (done + sizeof(pw_event_t) <= msg->o.size)) {
				memcpy((uint8_t *)msg->o.data + done, &f->q[f->head % EVQ_LEN], sizeof(pw_event_t));
				f->head++;
				done += sizeof(pw_event_t);
			}
			msg->o.err = (done != 0) ? (int)done : -EAGAIN;
			break;
		}

		case mtGetAttr:
			if (msg->i.attr.type == atMode) {
				msg->o.attr.val = S_IFCHR | 0666;
				msg->o.err = EOK;
			}
			else if (msg->i.attr.type == atPollStatus) {
				/* Low 16 bits: the requested events. The kernel may pack a
				 * block_ms above them for the single-inet path; ignore it. */
				srv.st[dev].pollstatus_msgs++;
				msg->o.attr.val = ((f != NULL) && (f->head != f->tail)) ? POLLIN : 0;
				if (msg->o.attr.val != 0) {
					srv.st[dev].pollstatus_ready++;
				}
				msg->o.err = EOK;
			}
			else {
				msg->o.err = -EINVAL;
			}
			break;

		case mtDevCtl: {
			const req_t *q = (const req_t *)msg->i.raw;
			if (q->magic != REQ_MAGIC) {
				msg->o.err = -EINVAL;
			}
			else if (q->op == OP_STATS) {
				memcpy(msg->o.raw, &srv.st[dev], sizeof(stats_t));
				msg->o.err = EOK;
			}
			else if (q->op == OP_QUIT) {
				srv.quit = 1;
				(void)condSignal(srv.qcond);
				msg->o.err = EOK;
			}
			else {
				msg->o.err = -EINVAL;
			}
			break;
		}

		default:
			msg->o.err = -ENOSYS;
			break;
	}
	mutexUnlock(srv.lock);
}


static void recv_thread(void *arg)
{
	int dev = (int)(intptr_t)arg;
	msg_t msg;
	msg_rid_t rid;

	for (;;) {
		if (msgRecv(srv.port[dev], &msg, &rid) < 0) {
			continue;
		}
		/* A read() of <= 64 bytes arrives packed: o.data points into THIS
		 * msg's o.raw, which is fine because it is answered from here. */
		handle(&msg, dev);
		(void)msgRespond(srv.port[dev], &msg, rid);
	}
}


static int start_thread(void (*fn)(void *), int prio, void *arg)
{
	void *stack = malloc(STACK_SZ);

	if (stack == NULL) {
		return -ENOMEM;
	}
	return beginthreadex(fn, prio, stack, STACK_SZ, arg, NULL);
}


static int server_main(int argc, char **argv)
{
	int i, opt, foreground = 0, readyfd = -1, probe;
	const char *path[NDEV] = { DEV_NOTIFY, DEV_LEGACY };
	oid_t oid;

	srv.period_us = 16667;
	srv.jitter_us = 8000;

	optind = 2;
	while ((opt = getopt(argc, argv, "fp:j:")) != -1) {
		switch (opt) {
			case 'f': foreground = 1; break;
			case 'p': srv.period_us = (uint32_t)atoi(optarg); break;
			case 'j': srv.jitter_us = (uint32_t)atoi(optarg); break;
			default:
				fprintf(stderr, "usage: pollwake server [-f] [-p period_us] [-j jitter_us]\n");
				return 2;
		}
	}
	for (i = optind; i < argc; i++) {
		if (strcmp(argv[i], "&") != 0) { /* psh passes a trailing & through */
			fprintf(stderr, "pollwake: unexpected argument '%s'\n", argv[i]);
			return 2;
		}
	}
	if ((srv.period_us < 1000) || (srv.jitter_us >= srv.period_us)) {
		fprintf(stderr, "pollwake: need period_us >= 1000 and jitter_us < period_us\n");
		return 2;
	}

	/* Detach before any port or thread exists (none survive a fork). The
	 * parent exits once the child has registered both devices. */
	if (foreground == 0) {
		int pfd[2];
		pid_t pid;
		char c = 0;

		if (pipe(pfd) < 0) {
			printf("POLLWAKE server FAIL pipe errno=%d\n", errno);
			return 1;
		}
		fflush(stdout);
		pid = fork();
		if (pid < 0) {
			printf("POLLWAKE server FAIL fork errno=%d\n", errno);
			return 1;
		}
		if (pid > 0) {
			close(pfd[1]);
			if ((read(pfd[0], &c, 1) != 1) || (c != 'R')) {
				printf("POLLWAKE server FAIL child did not come up\n");
				return 1;
			}
			printf("POLLWAKE server detached pid=%d\n", (int)pid);
			fflush(stdout);
			_exit(0);
		}
		close(pfd[0]);
		readyfd = pfd[1];
	}

	if ((mutexCreate(&srv.lock) < 0) || (condCreate(&srv.qcond) < 0)) {
		printf("POLLWAKE server FAIL mutex/cond\n");
		return 1;
	}
	for (i = 0; i < NDEV; i++) {
		if (portCreate(&srv.port[i]) < 0) {
			printf("POLLWAKE server FAIL portCreate\n");
			return 1;
		}
		oid.port = srv.port[i];
		oid.id = 0;
		if (create_dev(&oid, path[i]) < 0) {
			printf("POLLWAKE server FAIL create_dev %s (a second server in one boot cannot re-register)\n", path[i]);
			return 1;
		}
	}

	/* Does this kernel have pollNotify? A kernel without it answers -EINVAL
	 * (syscall number out of range); with it, notifying our own oid with no
	 * poller watching is a no-op that returns 0. */
	oid.port = srv.port[DEV_N];
	oid.id = 0;
	probe = pollNotify(&oid);
	srv.notify_ok = (probe == 0) ? 1 : 0;
	srv.st[DEV_N].notify_supported = srv.notify_ok;
	srv.st[DEV_N].notify_last_err = probe;

	if ((start_thread(recv_thread, 3, (void *)(intptr_t)DEV_N) < 0) ||
			(start_thread(recv_thread, 3, (void *)(intptr_t)DEV_L) < 0) ||
			(start_thread(event_thread, 2, NULL) < 0)) {
		printf("POLLWAKE server FAIL threads\n");
		return 1;
	}
	printf("POLLWAKE server ready notify_port=%u legacy_port=%u period_us=%u jitter_us=%u notify_supported=%d probe_rc=%d cntfrq=%llu\n",
		srv.port[DEV_N], srv.port[DEV_L], srv.period_us, srv.jitter_us, srv.notify_ok, probe,
		(unsigned long long)cnt_freq);
	fflush(stdout);

	if (readyfd >= 0) {
		const char c = 'R';
		(void)write(readyfd, &c, 1);
		close(readyfd);
	}

	mutexLock(srv.lock);
	while (srv.quit == 0) {
		(void)condWait(srv.qcond, srv.lock, 0);
	}
	mutexUnlock(srv.lock);
	/* Nothing is ever parked here, so no client can be left waiting; let the
	 * receive thread's OP_QUIT reply land before the ports die. */
	(void)usleep(50000);
	printf("POLLWAKE server quit events=%llu notifies=%llu notify_errs=%llu\n",
		(unsigned long long)srv.st[DEV_N].events, (unsigned long long)srv.st[DEV_N].notifies,
		(unsigned long long)srv.st[DEV_N].notify_errs);
	fflush(stdout);
	return 0;
}


/* ------------------------------------------------------------------ client */

static const char *const dev_path[NDEV] = { DEV_NOTIFY, DEV_LEGACY };
static const char *const dev_name[NDEV] = { "notify", "legacy" };


static int devctl(int dev, uint32_t op, stats_t *out)
{
	msg_t msg;
	oid_t oid;
	req_t *q = (req_t *)msg.i.raw;
	int err;

	if (lookup(dev_path[dev], NULL, &oid) < 0) {
		return -ENOENT;
	}
	memset(&msg, 0, sizeof(msg));
	msg.type = mtDevCtl;
	msg.oid = oid;
	q->magic = REQ_MAGIC;
	q->op = op;
	err = msgSend(oid.port, &msg);
	if (err >= 0) {
		err = msg.o.err;
	}
	if ((err >= 0) && (out != NULL)) {
		memcpy(out, msg.o.raw, sizeof(*out));
	}
	return err;
}


static int client_resolve(void)
{
	oid_t oid;
	int i;

	for (i = 0; i < 50; i++) {
		if ((lookup(DEV_NOTIFY, NULL, &oid) == 0) && (lookup(DEV_LEGACY, NULL, &oid) == 0)) {
			return 0;
		}
		(void)usleep(100000);
	}
	printf("POLLWAKE client FAIL cannot resolve %s / %s (run 'pollwake server' first)\n", DEV_NOTIFY, DEV_LEGACY);
	return -1;
}


static int cmp_u64(const void *a, const void *b)
{
	uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
	return (x < y) ? -1 : ((x > y) ? 1 : 0);
}


typedef struct {
	double p50, p90, p99, max, min;
} dist_t;

/* v[] in cntvct ticks; sorts v. */
static dist_t dist(uint64_t *v, size_t n)
{
	dist_t d;

	memset(&d, 0, sizeof(d));
	if (n == 0) {
		return d;
	}
	qsort(v, n, sizeof(*v), cmp_u64);
	d.min = cnt_to_us(v[0]);
	d.p50 = cnt_to_us(v[n / 2]);
	d.p90 = cnt_to_us(v[(n * 90) / 100]);
	d.p99 = cnt_to_us(v[(n * 99) / 100]);
	d.max = cnt_to_us(v[n - 1]);
	return d;
}


static void drain(int fd)
{
	pw_event_t ev[4];

	while (read(fd, ev, sizeof(ev)) > 0) {
	}
}


/*
 * Event latency on a device fd, alone or next to an idle second fd:
 *   extra = 0: { dev }            extra = 1: { dev, pipe read end }
 *   extra = 2: { dev, AF_UNIX socket }
 * Latency = client wake-up (right after poll() returns) - the server's stamp
 * when it queued the event. Every event read is counted, so a legacy wake-up
 * that finds several queued events records each of them.
 */
static int test_event(const char *name, int dev, int extra)
{
	uint64_t lat[N_EVENTS * 4];
	struct pollfd p[2];
	int fd, other[2] = { -1, -1 }, i, errs = 0, timeouts = 0, got = 0, nfds = 1;
	stats_t s0, s1;
	dist_t d;
	uint64_t msgs;

	fd = open(dev_path[dev], O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		printf("POLLWAKE %s dev=%s FAIL open errno=%d\n", name, dev_name[dev], errno);
		return 1;
	}
	if (extra == 1) {
		if (pipe(other) < 0) {
			printf("POLLWAKE %s FAIL pipe errno=%d\n", name, errno);
			close(fd);
			return 1;
		}
	}
	else if (extra == 2) {
		if (socketpair(AF_UNIX, SOCK_STREAM, 0, other) < 0) {
			printf("POLLWAKE %s FAIL socketpair errno=%d\n", name, errno);
			close(fd);
			return 1;
		}
	}
	if (extra != 0) {
		nfds = 2;
	}

	drain(fd);
	(void)devctl(dev, OP_STATS, &s0);
	for (i = 0; (i < N_EVENTS) && (got < N_EVENTS); i++) {
		pw_event_t ev[4]; /* a 20 ms legacy sleep can find 3 events queued (8.7 ms minimum spacing) */
		uint64_t now;
		ssize_t rc;
		int k, r;

		p[0].fd = fd;
		p[0].events = POLLIN;
		p[0].revents = 0;
		p[1].fd = other[0];
		p[1].events = POLLIN;
		p[1].revents = 0;
		r = poll(p, (nfds_t)nfds, 1000);
		now = now_cnt();
		if (r == 0) {
			timeouts++;
			continue;
		}
		if ((r < 0) || ((p[0].revents & POLLIN) == 0) || ((nfds == 2) && (p[1].revents != 0))) {
			errs++;
			continue;
		}
		rc = read(fd, ev, sizeof(ev));
		if (rc <= 0) {
			errs++;
			continue;
		}
		for (k = 0; k < (int)(rc / (ssize_t)sizeof(pw_event_t)); k++) {
			lat[got++] = (now > ev[k].t_event) ? now - ev[k].t_event : 0;
		}
	}
	(void)devctl(dev, OP_STATS, &s1);
	close(fd);
	if (other[0] >= 0) {
		close(other[0]);
		close(other[1]);
	}
	d = dist(lat, (size_t)got);
	msgs = s1.pollstatus_msgs - s0.pollstatus_msgs;
	printf("POLLWAKE %s dev=%s events=%d errs=%d timeouts=%d p50_us=%.1f p90_us=%.1f p99_us=%.1f max_us=%.1f min_us=%.1f "
		"pollstatus_msgs=%llu per_event=%.2f notifies=%llu notify_errs=%llu\n",
		name, dev_name[dev], got, errs, timeouts, d.p50, d.p90, d.p99, d.max, d.min, (unsigned long long)msgs,
		(got != 0) ? (double)msgs / (double)got : 0.0, (unsigned long long)(s1.notifies - s0.notifies),
		(unsigned long long)(s1.notify_errs - s0.notify_errs));
	fflush(stdout);
	return ((errs == 0) && (timeouts == 0)) ? 0 : 1;
}


/* ---- a non-notifying fd next to a server fd must still wake (pipewake/unixwake) */

typedef struct {
	int fd;
	int n;
} writer_arg_t;

static void *writer_fn(void *arg)
{
	writer_arg_t *a = arg;
	uint64_t t;
	int i;

	for (i = 0; i < a->n; i++) {
		(void)usleep(5000u + (rng() % 20000u));
		t = now_cnt();
		if (write(a->fd, &t, sizeof(t)) != (ssize_t)sizeof(t)) {
			break;
		}
	}
	return NULL;
}


/*
 * { legacy dev (POLLOUT: never ready), pipe or AF_UNIX read end }, a thread
 * writes a timestamp at random 5-25 ms intervals. Latency = wake - stamp.
 * pipe (posixsrv, does not notify): must stay within the 20 ms fallback.
 * unix: woken by the AF_UNIX readiness queue, must stay sub-millisecond.
 */
static int test_otherwake(const char *name, int unixsock)
{
	uint64_t lat[N_EVENTS];
	struct pollfd p[2];
	int fd, other[2], i, errs = 0, timeouts = 0, got = 0;
	pthread_t th;
	writer_arg_t wa;
	dist_t d;

	fd = open(DEV_LEGACY, O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		printf("POLLWAKE %s FAIL open errno=%d\n", name, errno);
		return 1;
	}
	if (((unixsock != 0) ? socketpair(AF_UNIX, SOCK_STREAM, 0, other) : pipe(other)) < 0) {
		printf("POLLWAKE %s FAIL pipe/socketpair errno=%d\n", name, errno);
		close(fd);
		return 1;
	}
	wa.fd = other[1];
	wa.n = N_EVENTS;
	if (pthread_create(&th, NULL, writer_fn, &wa) != 0) {
		printf("POLLWAKE %s FAIL pthread_create\n", name);
		return 1;
	}
	for (i = 0; (i < N_EVENTS * 2) && (got < N_EVENTS); i++) {
		uint64_t t, now;
		int r;

		p[0].fd = fd;
		p[0].events = POLLOUT;
		p[0].revents = 0;
		p[1].fd = other[0];
		p[1].events = POLLIN;
		p[1].revents = 0;
		r = poll(p, 2, 1000);
		now = now_cnt();
		if (r == 0) {
			timeouts++;
			continue;
		}
		if ((r < 0) || ((p[1].revents & POLLIN) == 0)) {
			errs++;
			continue;
		}
		if (read(other[0], &t, sizeof(t)) != (ssize_t)sizeof(t)) {
			errs++;
			continue;
		}
		lat[got++] = (now > t) ? now - t : 0;
	}
	(void)pthread_join(th, NULL);
	close(fd);
	close(other[0]);
	close(other[1]);
	d = dist(lat, (size_t)got);
	printf("POLLWAKE %s events=%d errs=%d timeouts=%d p50_us=%.1f p90_us=%.1f p99_us=%.1f max_us=%.1f min_us=%.1f\n",
		name, got, errs, timeouts, d.p50, d.p90, d.p99, d.max, d.min);
	fflush(stdout);
	return ((errs == 0) && (timeouts == 0) && (got == N_EVENTS)) ? 0 : 1;
}


/*
 * A poll that nothing satisfies must still time out on time, although the
 * device's notifies wake the sleep ~60 times a second (every wake re-queries
 * and goes back to sleep): { notify dev, POLLOUT }, poll(..., 100) x 10.
 */
static int test_timeout(void)
{
	const int n = 10, tmo_ms = 100;
	struct pollfd p;
	int fd, i, bad = 0, early = 0;
	uint64_t t0, el, mn = UINT64_MAX, mx = 0;
	stats_t s0, s1;

	fd = open(DEV_NOTIFY, O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		printf("POLLWAKE timeout FAIL open errno=%d\n", errno);
		return 1;
	}
	(void)devctl(DEV_N, OP_STATS, &s0);
	for (i = 0; i < n; i++) {
		p.fd = fd;
		p.events = POLLOUT;
		p.revents = 0;
		t0 = now_cnt();
		if (poll(&p, 1, tmo_ms) != 0) {
			bad++;
		}
		el = now_cnt() - t0;
		if (cnt_to_us(el) < (double)tmo_ms * 1000.0) {
			early++;
		}
		mn = (el < mn) ? el : mn;
		mx = (el > mx) ? el : mx;
		drain(fd);
	}
	(void)devctl(DEV_N, OP_STATS, &s1);
	close(fd);
	printf("POLLWAKE timeout n=%d rc_nonzero=%d early=%d elapsed_ms_min=%.1f elapsed_ms_max=%.1f pollstatus_msgs=%llu\n",
		n, bad, early, cnt_to_us(mn) / 1000.0, cnt_to_us(mx) / 1000.0,
		(unsigned long long)(s1.pollstatus_msgs - s0.pollstatus_msgs));
	fflush(stdout);
	return ((bad == 0) && (early == 0) && (cnt_to_us(mx) < (tmo_ms + 25) * 1000.0)) ? 0 : 1;
}


/* ---- a caught signal during poll() on a server fd */

static volatile int alarm_ran;

static void on_alarm(int sig)
{
	(void)sig;
	alarm_ran = 1;
}

static void eintr_one(const char *set, int mixed)
{
	struct pollfd p[2];
	struct sigaction sa;
	int fd, sv[2] = { -1, -1 }, rc, e;
	uint64_t t0;
	double ms;

	fd = open(DEV_LEGACY, O_RDONLY | O_NONBLOCK);
	if ((fd < 0) || ((mixed != 0) && (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0))) {
		printf("POLLWAKE eintr set=%s FAIL open/socketpair errno=%d\n", set, errno);
		if (fd >= 0) {
			close(fd);
		}
		return;
	}
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_alarm;
	(void)sigaction(SIGALRM, &sa, NULL);
	alarm_ran = 0;
	p[0].fd = fd;
	p[0].events = POLLOUT;
	p[0].revents = 0;
	p[1].fd = sv[0];
	p[1].events = POLLIN;
	p[1].revents = 0;
	t0 = now_cnt();
	(void)alarm(1);
	rc = poll(p, (mixed != 0) ? 2 : 1, 3000);
	e = errno;
	ms = cnt_to_us(now_cnt() - t0) / 1000.0;
	(void)alarm(0);
	close(fd);
	if (sv[0] >= 0) {
		close(sv[0]);
		close(sv[1]);
	}
	printf("POLLWAKE eintr set=%s rc=%d errno=%d elapsed_ms=%.1f handler_ran=%d\n", set, rc, (rc < 0) ? e : 0, ms,
		alarm_ran);
	fflush(stdout);
}

/*
 * Signal behaviour must be what it was (informational, never a failure):
 *   set=server { legacy dev }: the old timed sleep ignored the interruption, so
 *     poll() returns 0 at its 3000 ms timeout and the handler runs then;
 *   set=mixed { legacy dev, AF_UNIX }: the AF_UNIX wait was interruptible, so
 *     poll() returns -1/EINTR at ~1000 ms.
 */
static int test_eintr(void)
{
	eintr_one("server", 0);
	eintr_one("mixed", 1);
	return 0;
}


static int client_main(int argc, char **argv)
{
	const char *t = (argc > 2) ? argv[2] : "all";
	int all = (strcmp(t, "all") == 0), fails = 0, d;
	stats_t st;

	if (client_resolve() < 0) {
		return 1;
	}
	if (strcmp(t, "quit") == 0) {
		printf("POLLWAKE quit rc=%d\n", devctl(DEV_N, OP_QUIT, NULL));
		fflush(stdout);
		return 0;
	}
	(void)devctl(DEV_N, OP_STATS, &st);
	printf("POLLWAKE client start test=%s notify_supported=%lld probe_rc=%lld cntfrq=%llu\n", t,
		(long long)st.notify_supported, (long long)st.notify_last_err, (unsigned long long)cnt_freq);
	fflush(stdout);

	for (d = 0; d < NDEV; d++) {
		if (all || (strcmp(t, "single") == 0)) {
			fails += test_event("single", d, 0);
		}
		if (all || (strcmp(t, "pipe") == 0)) {
			fails += test_event("pipe", d, 1);
		}
		if (all || (strcmp(t, "unix") == 0)) {
			fails += test_event("unix", d, 2);
		}
	}
	if (all || (strcmp(t, "pipewake") == 0)) {
		fails += test_otherwake("pipewake", 0);
	}
	if (all || (strcmp(t, "unixwake") == 0)) {
		fails += test_otherwake("unixwake", 1);
	}
	if (all || (strcmp(t, "timeout") == 0)) {
		fails += test_timeout();
	}
	if (all || (strcmp(t, "eintr") == 0)) {
		fails += test_eintr();
	}
	printf("POLLWAKE client done test=%s fails=%d\n", t, fails);
	fflush(stdout);
	return (fails == 0) ? 0 : 1;
}


int main(int argc, char **argv)
{
	time_init();
	rng_state ^= (uint32_t)now_cnt();

	if ((argc >= 2) && (strcmp(argv[1], "server") == 0)) {
		return server_main(argc, argv);
	}
	if ((argc >= 2) && (strcmp(argv[1], "client") == 0)) {
		return client_main(argc, argv);
	}
	fprintf(stderr, "usage: pollwake server [-f] [-p period_us] [-j jitter_us]\n"
					"       pollwake client {single|pipe|unix|pipewake|unixwake|timeout|eintr|all|quit}\n");
	return 2;
}
