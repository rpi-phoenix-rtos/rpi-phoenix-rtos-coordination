/*
 * Host harness for posixsrv's timed requests.
 *
 * Runs posixsrv's REAL posixsrv.c, event.c and pty.c (with the real libtty)
 * on host threads -- the same 4 server threads and timeout thread as srv.c
 * starts -- and drives them with messages the way client processes would (see
 * shim.c). Every client call has a deadline, so a request posixsrv never
 * answers, or a posixsrv that deadlocked, fails the case instead of hanging
 * the run.
 *
 * The cases go after the requests that wait with a timeout: pty slave reads
 * under VMIN/VTIME and event-queue reads with a timeout, completed by the
 * timeout thread or by the write that arrives first.
 *
 *   posixsrv-host [-n iterations] [case ...]
 */
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/events.h>
#include <sys/threads.h>

#include "posixsrv.h"
#include "shim.h"

#define CALL_TIMEOUT_MS 5000

static unsigned srvPort, evPort;
static oid_t ptmxOid, qmxOid, sinkOid;
static int iterations = 200;
static int failures;


#define FAIL(...) \
	do { \
		fprintf(stderr, "  FAIL %s:%d: ", __func__, __LINE__); \
		fprintf(stderr, __VA_ARGS__); \
		fprintf(stderr, "\n"); \
		__atomic_fetch_add(&failures, 1, __ATOMIC_RELAXED); \
	} while (0)


static time_t now_ms(void)
{
	time_t t;
	gettime(&t, NULL);
	return t / 1000;
}


/* Sleep for ms of the fake clock */
static void sleep_ms(int ms, int scale)
{
	usleep((useconds_t)ms * 1000 / scale);
}


static int call(oid_t oid, msg_t *m)
{
	int err;

	m->oid = oid;
	err = msgSend(oid.port, m);
	return (err < 0) ? err : m->o.err;
}


/* ---- pty client ---- */

typedef struct {
	oid_t master, slave;
} pty_t;


static int pty_ioctl(oid_t oid, unsigned long request, const void *in, void *out)
{
	msg_t m;

	shim_ioctlPack(&m, oid.id, request, in, out);
	return call(oid, &m);
}


static int pty_open(pty_t *p, int vmin, int vtime)
{
	msg_t m;
	int err, unlock = 0;
	unsigned id;
	struct termios t;

	memset(&m, 0, sizeof(m));
	m.type = mtOpen;
	if ((err = call(ptmxOid, &m)) < 0) {
		return err;
	}
	p->master.port = srvPort;
	p->master.id = err;

	if ((err = pty_ioctl(p->master, TIOCSPTLCK, &unlock, NULL)) < 0 ||
			(err = pty_ioctl(p->master, TIOCGPTN, NULL, &id)) < 0) {
		return err;
	}
	p->slave.port = srvPort;
	p->slave.id = id;

	memset(&m, 0, sizeof(m));
	m.type = mtOpen;
	m.i.openclose.flags = O_RDWR | O_NOCTTY;
	if ((err = call(p->slave, &m)) < 0) {
		return err;
	}

	if ((err = pty_ioctl(p->slave, TCGETS, NULL, &t)) < 0) {
		return err;
	}
	t.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
	t.c_oflag &= ~OPOST;
	t.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
	t.c_cc[VMIN] = vmin;
	t.c_cc[VTIME] = vtime;
	return pty_ioctl(p->slave, TCSETS, &t, NULL);
}


static int pty_read(pty_t *p, void *buf, size_t len)
{
	msg_t m;

	memset(&m, 0, sizeof(m));
	m.type = mtRead;
	m.o.data = buf;
	m.o.size = len;
	return call(p->slave, &m);
}


static int pty_write(pty_t *p, const void *buf, size_t len)
{
	msg_t m;

	memset(&m, 0, sizeof(m));
	m.type = mtWrite;
	m.i.data = buf;
	m.i.size = len;
	return call(p->master, &m);
}


static int pty_closeEnd(oid_t oid)
{
	msg_t m;

	memset(&m, 0, sizeof(m));
	m.type = mtClose;
	return call(oid, &m);
}


static void pty_close(pty_t *p)
{
	pty_closeEnd(p->slave);
	pty_closeEnd(p->master);
}


/* ---- event-queue client ---- */

static int evq_open(oid_t *q)
{
	msg_t m;
	int err;

	memset(&m, 0, sizeof(m));
	m.type = mtOpen;
	if ((err = call(qmxOid, &m)) < 0) {
		return err;
	}
	q->port = evPort;
	q->id = err;
	return 0;
}


/* Read events with a timeout (ms); subscribe to evtDataIn of watched first, if given */
static int evq_read(oid_t q, const oid_t *watched, event_t *ev, int evcnt, int timeout)
{
	msg_t m;
	evsub_t sub;

	memset(&m, 0, sizeof(m));
	m.type = mtRead;
	if (watched != NULL) {
		memset(&sub, 0, sizeof(sub));
		sub.oid = *watched;
		sub.flags = evAdd;
		sub.types = 1 << evtDataIn;
		m.i.data = &sub;
		m.i.size = sizeof(sub);
	}
	m.o.data = ev;
	m.o.size = evcnt * sizeof(*ev);
	m.i.io.len = timeout;
	return call(q, &m);
}


static int evq_post(const oid_t *watched)
{
	msg_t m;
	event_t ev;

	memset(&ev, 0, sizeof(ev));
	ev.oid = *watched;
	ev.type = evtDataIn;
	ev.count = 1;

	memset(&m, 0, sizeof(m));
	m.type = mtWrite;
	m.i.data = &ev;
	m.i.size = sizeof(ev);
	return call(sinkOid, &m);
}


static int evq_close(oid_t q)
{
	msg_t m;

	memset(&m, 0, sizeof(m));
	m.type = mtClose;
	return call(q, &m);
}


/* ---- cases ---- */

static pthread_barrier_t bar;


/* A slave read on another thread; answers what the read answered */
static void *slave_reader(void *arg)
{
	char buf[8];

	shim_setCallTimeout(CALL_TIMEOUT_MS);
	return (void *)(long)pty_read(arg, buf, sizeof(buf));
}


static void *slave_readerSync(void *arg)
{
	char buf[8];

	shim_setCallTimeout(2000);
	pthread_barrier_wait(&bar);
	return (void *)(long)pty_read(arg, buf, sizeof(buf));
}

/*
 * A short timeout armed after a long one must still expire first. The timeout
 * tree is ordered by wakeup time and the thread sleeps until its minimum.
 */
static void case_pty_order(void)
{
	pty_t a, b;
	char buf[8];
	time_t t0, dt;
	pthread_t th;
	int scale = 4;

	shim_setClock(scale, 1);
	if (pty_open(&a, 0, 20) < 0 || pty_open(&b, 0, 1) < 0) {
		FAIL("pty_open");
		return;
	}

	/* a: a 2 s read, armed first, on another thread */
	pthread_create(&th, NULL, slave_reader, &a);
	sleep_ms(50, scale);

	t0 = now_ms();
	int n = pty_read(&b, buf, sizeof(buf));
	dt = now_ms() - t0;
	if (n != 0) {
		FAIL("100 ms read answered %d, expected 0 (timeout)", n);
	}
	if (dt > 600) {
		FAIL("100 ms read took %ld ms: the timeout thread slept until the 2 s request's deadline", (long)dt);
	}
	printf("  100 ms VTIME read armed after a 2 s one returned after %ld ms\n", (long)dt);

	void *ret;
	pthread_join(th, &ret);
	if ((long)ret != 0) {
		FAIL("2 s read answered %ld", (long)ret);
	}
	pty_close(&a);
	pty_close(&b);
}


/*
 * Two requests due at the same moment (a coarse clock makes it likely) must
 * both expire. lib_rbInsert() does not insert a node that compares equal to one
 * already in the tree.
 */
static void case_pty_tie(void)
{
	enum { N = 4 };
	pty_t p[N];
	pthread_t th[N];
	int scale = 4, i, round, hung = 0;

	/* 50 ms steps: readers armed together get the same wakeup */
	shim_setClock(scale, 50000);
	for (i = 0; i < N; ++i) {
		if (pty_open(&p[i], 0, 1) < 0) {
			FAIL("pty_open");
			return;
		}
	}

	for (round = 0; round < 10 && !hung; ++round) {
		pthread_barrier_init(&bar, NULL, N);
		for (i = 0; i < N; ++i) {
			pthread_create(&th[i], NULL, slave_readerSync, &p[i]);
		}
		for (i = 0; i < N; ++i) {
			void *ret;
			pthread_join(th[i], &ret);
			if ((long)ret == -ETIMEDOUT) {
				hung++;
			}
			else if ((long)ret != 0) {
				FAIL("read answered %ld", (long)ret);
			}
		}
		pthread_barrier_destroy(&bar);
	}
	if (hung) {
		FAIL("%d of %d reads due at the same time NEVER expired (round %d)", hung, N, round);
	}
	shim_setClock(1, 1);
	for (i = 0; i < N; ++i) {
		pty_close(&p[i]); /* also answers the reads that never expired */
	}
}


/*
 * VMIN=5 VTIME=1 with only 2 bytes there: the inter-byte timer expires and the
 * read must return those 2 bytes (POSIX: VMIN>0, VTIME>0).
 */
static void *vmin_writer(void *arg)
{
	shim_setCallTimeout(CALL_TIMEOUT_MS);
	usleep(20000 / 4);
	pty_write(arg, "cd", 2);
	return NULL;
}


static void case_pty_vmin(void)
{
	pty_t p;
	char buf[16];
	int n, scale = 4;

	shim_setClock(scale, 1);
	if (pty_open(&p, 5, 1) < 0) {
		FAIL("pty_open");
		return;
	}

	/* bytes already there when the read starts */
	pty_write(&p, "ab", 2);
	n = pty_read(&p, buf, sizeof(buf));
	if (n != 2 || memcmp(buf, "ab", 2) != 0) {
		FAIL("read with 2 of VMIN=5 bytes queued answered %d, expected 2", n);
	}

	/* bytes arriving while the read waits */
	pthread_t th;
	pthread_create(&th, NULL, vmin_writer, &p);
	n = pty_read(&p, buf, sizeof(buf));
	pthread_join(th, NULL);
	if (n != 2 || memcmp(buf, "cd", 2) != 0) {
		FAIL("read woken by 2 of VMIN=5 bytes answered %d, expected 2", n);
	}
	pty_close(&p);
}


/*
 * The race itself: slave reads with VTIME=1 completed either by their timeout
 * or by a master write landing near the deadline. No byte may be lost or
 * duplicated, no read may go unanswered.
 */
typedef struct {
	pty_t p;
	int scale;
	long written, read;
	int done;
	unsigned seed;
} race_t;


static void *race_writer(void *arg)
{
	race_t *r = arg;
	char buf[4] = "wxyz";
	int i, n;

	shim_setCallTimeout(CALL_TIMEOUT_MS);
	for (i = 0; i < iterations; ++i) {
		/* around the 100 ms deadline of the read in progress */
		usleep((80000 + rand_r(&r->seed) % 40000) / r->scale);
		n = 1 + rand_r(&r->seed) % 4;
		if (pty_write(&r->p, buf, n) == n) {
			r->written += n;
		}
	}
	return NULL;
}


static void *race_reader(void *arg)
{
	race_t *r = arg;
	char buf[64];
	int n, idle = 0;

	shim_setCallTimeout(CALL_TIMEOUT_MS);
	for (;;) {
		n = pty_read(&r->p, buf, sizeof(buf));
		if (n < 0) {
			FAIL("read answered %d%s", n, (n == -ETIMEDOUT) ? " (never answered)" : "");
			return NULL;
		}
		r->read += n;
		/* the writer is done once it stops, a few idle timeouts in a row */
		idle = (n == 0) ? idle + 1 : 0;
		if (idle > 3 && __atomic_load_n(&r->done, __ATOMIC_ACQUIRE) && (r->written == r->read || idle > 30)) {
			return NULL;
		}
	}
}


static void case_pty_race(void)
{
	enum { N = 8 };
	race_t r[N];
	pthread_t tw[N], tr[N];
	int i, scale = 8;

	shim_setClock(scale, 1);
	memset(r, 0, sizeof(r));
	for (i = 0; i < N; ++i) {
		r[i].scale = scale;
		r[i].seed = 1 + i;
		if (pty_open(&r[i].p, 0, 1) < 0) {
			FAIL("pty_open");
			return;
		}
	}
	for (i = 0; i < N; ++i) {
		pthread_create(&tr[i], NULL, race_reader, &r[i]);
		pthread_create(&tw[i], NULL, race_writer, &r[i]);
	}
	for (i = 0; i < N; ++i) {
		pthread_join(tw[i], NULL);
		__atomic_store_n(&r[i].done, 1, __ATOMIC_RELEASE);
	}
	for (i = 0; i < N; ++i) {
		pthread_join(tr[i], NULL);
		if (r[i].read != r[i].written) {
			FAIL("pty %d: wrote %ld bytes, read %ld", i, r[i].written, r[i].read);
		}
	}
	long total = 0;
	for (i = 0; i < N; ++i) {
		total += r[i].written;
		pty_close(&r[i].p);
	}
	printf("  %d ptys x %d writes near the VTIME deadline, %ld bytes through\n", N, iterations, total);
}


/*
 * A slave read with a timeout pending while the slave and then the master
 * close: the close answers the read, after which its timeout must not fire
 * on it, nor on the pty (freed with the master).
 */
static void case_pty_close(void)
{
	int i, scale = 4;

	shim_setClock(scale, 1);
	for (i = 0; i < iterations / 10 + 1; ++i) {
		pty_t p;
		pthread_t th;

		if (pty_open(&p, 0, 1) < 0) {
			FAIL("pty_open");
			return;
		}
		pthread_create(&th, NULL, slave_reader, &p);
		usleep((i % 20) * 5000 / scale); /* 0..95 ms into the 100 ms */
		pty_close(&p);
		void *ret;
		pthread_join(th, &ret);
		if ((long)ret == -ETIMEDOUT) {
			FAIL("read never answered after close");
		}
	}
	sleep_ms(300, scale); /* let any timeout still in the tree fire */
}


/* An event-queue read that times out answers 0 events. */
static void case_ev_timeout(void)
{
	oid_t q, watched = { .port = 99, .id = 1 };
	event_t ev[4];
	time_t t0, dt;
	int n;

	shim_setClock(1, 1);
	if (evq_open(&q) < 0) {
		FAIL("evq_open");
		return;
	}
	t0 = now_ms();
	n = evq_read(q, &watched, ev, 4, 50);
	dt = now_ms() - t0;
	if (n == -ETIMEDOUT) {
		FAIL("a 50 ms event read was NEVER answered");
	}
	else if (n != 0) {
		FAIL("a 50 ms event read with no events answered %d", n);
	}
	else {
		printf("  50 ms event read answered 0 after %ld ms\n", (long)dt);
	}
	evq_close(q);
}


/* Event-queue reads with short timeouts racing events posted to the sink. */
typedef struct {
	oid_t q, watched;
	long posted, got;
	int done;
	unsigned seed;
} evrace_t;


static void *evrace_reader(void *arg)
{
	evrace_t *r = arg;
	event_t ev[4];
	int n, first = 1, idle = 0;

	shim_setCallTimeout(CALL_TIMEOUT_MS);
	for (;;) {
		n = evq_read(r->q, first ? &r->watched : NULL, ev, 4, 2);
		first = 0;
		if (n < 0) {
			FAIL("event read answered %d%s", n, (n == -ETIMEDOUT) ? " (never answered)" : "");
			return NULL;
		}
		if (n > 0) {
			r->got++;
		}
		idle = (n == 0) ? idle + 1 : 0;
		if (idle > 3 && __atomic_load_n(&r->done, __ATOMIC_ACQUIRE)) {
			return NULL;
		}
	}
}


static void *evrace_writer(void *arg)
{
	evrace_t *r = arg;
	int i;

	shim_setCallTimeout(CALL_TIMEOUT_MS);
	for (i = 0; i < iterations * 5; ++i) {
		usleep(1000 + rand_r(&r->seed) % 2000); /* around the 2 ms deadline */
		if (evq_post(&r->watched) == 0) {
			r->posted++;
		}
	}
	return NULL;
}


static void case_ev_race(void)
{
	enum { N = 8 };
	evrace_t r[N];
	pthread_t tw[N], tr[N];
	int i;
	long posted = 0, got = 0;

	shim_setClock(1, 1);
	memset(r, 0, sizeof(r));
	for (i = 0; i < N; ++i) {
		r[i].watched.port = 99;
		r[i].watched.id = 100 + i;
		r[i].seed = 7 + i;
		if (evq_open(&r[i].q) < 0) {
			FAIL("evq_open");
			return;
		}
	}
	for (i = 0; i < N; ++i) {
		pthread_create(&tr[i], NULL, evrace_reader, &r[i]);
	}
	usleep(10000); /* subscribed */
	for (i = 0; i < N; ++i) {
		pthread_create(&tw[i], NULL, evrace_writer, &r[i]);
	}
	for (i = 0; i < N; ++i) {
		pthread_join(tw[i], NULL);
		__atomic_store_n(&r[i].done, 1, __ATOMIC_RELEASE);
	}
	for (i = 0; i < N; ++i) {
		pthread_join(tr[i], NULL);
		posted += r[i].posted;
		got += r[i].got;
		evq_close(r[i].q);
	}
	/* several posts may coalesce into one pending event, so got <= posted */
	printf("  %d queues: %ld events posted, %ld reads returned events\n", N, posted, got);
	if (got == 0) {
		FAIL("no event was ever delivered");
	}
}


/* ---- runner ---- */

static const struct {
	const char *name;
	void (*fn)(void);
} cases[] = {
	{ "pty_order", case_pty_order },
	{ "pty_tie", case_pty_tie },
	{ "pty_vmin", case_pty_vmin },
	{ "pty_race", case_pty_race },
	{ "pty_close", case_pty_close },
	{ "ev_timeout", case_ev_timeout },
	{ "ev_race", case_ev_race },
};


static void *thread_main(void *arg)
{
	posixsrv_threadMain(arg);
	return NULL;
}


static void *thread_timeout(void *arg)
{
	posixsrv_threadRqTimeout(arg);
	return NULL;
}


/* posixsrv still answers: its table lock is free and a server thread is too */
static int posixsrv_alive(void)
{
	msg_t m;
	int err;

	shim_setCallTimeout(3000);
	memset(&m, 0, sizeof(m));
	m.type = mtOpen;
	err = call(ptmxOid, &m);
	if (err >= 0) {
		oid_t master = { .port = srvPort, .id = err };
		pty_closeEnd(master);
	}
	return err != -ETIMEDOUT;
}


int main(int argc, char *argv[])
{
	pthread_t th;
	int i, j, opt, ran = 0;

	while ((opt = getopt(argc, argv, "n:")) != -1) {
		if (opt == 'n') {
			iterations = atoi(optarg);
		}
		else {
			fprintf(stderr, "usage: %s [-n iterations] [case ...]\n", argv[0]);
			return 2;
		}
	}

	if (posixsrv_init(&srvPort, &evPort) < 0) {
		fprintf(stderr, "posixsrv_init failed\n");
		return 2;
	}
	/* as srv.c: one thread on the event port, three on the server port, this one on timeouts */
	pthread_create(&th, NULL, thread_main, (void *)(uintptr_t)evPort);
	for (i = 0; i < 3; ++i) {
		pthread_create(&th, NULL, thread_main, (void *)(uintptr_t)srvPort);
	}
	pthread_create(&th, NULL, thread_timeout, NULL);
	shim_waitServed(srvPort);
	shim_waitServed(evPort);

	if (lookup("/dev/ptmx", &ptmxOid, NULL) < 0 || lookup("/dev/event/queue", &qmxOid, NULL) < 0 ||
			lookup("/dev/event/sink", &sinkOid, NULL) < 0) {
		fprintf(stderr, "posixsrv registered no /dev/ptmx or /dev/event\n");
		return 2;
	}

	for (i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); ++i) {
		int want = (optind == argc);
		for (j = optind; j < argc; ++j) {
			want |= (strcmp(argv[j], cases[i].name) == 0);
		}
		if (!want) {
			continue;
		}

		int before = failures;
		printf("%s\n", cases[i].name);
		fflush(stdout);
		shim_setCallTimeout(CALL_TIMEOUT_MS);
		cases[i].fn();
		ran++;
		printf("%s: %s\n", cases[i].name, (failures == before) ? "PASS" : "FAIL");
		fflush(stdout);

		if (!posixsrv_alive()) {
			printf("posixsrv DEADLOCKED: a new open() is not answered -- stopping\n");
			return 1;
		}
	}

	printf("%d case(s), %d failure(s)\n", ran, failures);
	return (failures == 0) ? 0 : 1;
}
