/*
 * ipcprobe -- IPC semantics the new-lane render and display servers depend on
 *
 * Experiment E5 of docs/gpu-new-lane/PLAN.md; pre-registration and the kernel
 * reading behind every test: docs/gpu-new-lane/E5-deferred-reply.md.
 *
 *   ipcprobe server [-r recv_threads] [-t tick_us] [-v vblank_us]
 *       Registers /dev/ipcprobe. One or more threads msgRecv(); requests are
 *       either answered at once or parked and answered LATER from another
 *       thread: a "worker" (deadlines, timeouts) or a higher-priority "irq"
 *       thread that advances a fence seqno every tick_us and raises an emulated
 *       vblank every vblank_us. It serves a vblank event stream on read() and
 *       poll() of /dev/ipcprobe, and publishes a fence page (one cached page)
 *       whose physical address clients map read-only with MAP_PHYSMEM.
 *       Prints one banner, then stays silent until it is told to quit.
 *
 *   ipcprobe client <test>
 *       rtt deferred timeout wait read poll fence fence-rw fence-ro signal
 *       ridreuse all quit
 *
 * Every result is one line starting with "IPCPROBE <test>".
 *
 * Build (standalone, tree sysroot; see the E5 doc):
 *   S=.buildroot/_build/aarch64a72-generic-rpi4b/sysroot
 *   .toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc -O2 -Wall -Wextra -std=gnu11 \
 *       --sysroot=$S/ -B$S/lib/ -o tools/gpu-lane/ipcprobe/ipcprobe \
 *       tools/gpu-lane/ipcprobe/ipcprobe.c -lpthread
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
#include <sys/mman.h>
#include <sys/msg.h>
#include <sys/threads.h>
#include <sys/wait.h>
#include <phoenix/file.h>
#include <posix/utils.h>

#ifndef ECANCELED
#define ECANCELED EIO
#endif

#define DEV_PATH   "/dev/ipcprobe"
#define REQ_MAGIC  0x45355052u /* "E5PR" */
#define MAX_SLOTS  256
#define MAX_FILES  32
#define STACK_SZ   (32u * 1024u)
#define PAGE_SZ    4096u
#define PAYLOAD_SZ 4096u

/* Request/response carried in msg.i.raw / msg.o.raw of an mtDevCtl message.
 * mtDevCtl is never packed by the kernel (msg.c msg_ipack/msg_opack return
 * early for it), so i.data/o.data, when set, are page-mapped into the server. */
enum {
	OP_ECHO = 1,     /* respond at once from the receiving thread */
	OP_ECHO_DEFER,   /* hand to the worker, which responds (handoff cost) */
	OP_DELAY,        /* worker responds after delay_ms; fills o.data, sums i.data */
	OP_WAIT,         /* irq thread responds when fence seq >= arg; worker on timeout */
	OP_FENCE_INFO,   /* PA of the fence page etc. */
	OP_FENCE_PEEK,   /* server reads the page's client-scratch word */
	OP_STATS,        /* server counters */
	OP_STALE,        /* respond at once, remember the rid (for OP_HOLD) */
	OP_HOLD,         /* deferred delay_ms; if it reuses OP_STALE's rid, a late
	                  * duplicate response to the OLD request is sent first */
	OP_BADRID,       /* server calls msgRespond with a rid that was never issued */
	OP_QUIT
};

typedef struct {
	uint32_t magic;
	uint32_t op;
	uint32_t token;
	uint32_t delay_ms;
	uint64_t arg;
	uint64_t t_send;
} req_t;

typedef struct {
	int32_t err;
	uint32_t token;
	int32_t rid;
	uint32_t flags;
	uint64_t t_recv; /* server: cntvct when msgRecv returned */
	uint64_t t_resp; /* server: cntvct just before msgRespond (or fence publish) */
	uint64_t val;
	uint64_t val2;
} resp_t;

typedef struct {
	uint64_t max_outstanding;
	uint64_t recv_while_outstanding;
	uint64_t pollstatus_msgs;
	uint64_t pollstatus_with_block;
	int64_t late_respond_rc;
	int64_t dup_respond_rc;
	uint64_t ticks_skipped;
	uint64_t responds_from_irq;
} stats_t;

/* One vblank event, returned by read() of /dev/ipcprobe. */
typedef struct {
	uint64_t seq;
	uint64_t t_vblank;
	uint64_t missed;
	uint64_t t_resp;
} vbl_event_t;

/* The fence page. seq/t_publish/vblank are published under a seqlock (gen odd
 * while writing) only because the TIMESTAMP is published next to the seqno; a
 * single aligned 64-bit seqno per queue needs no lock (single-copy atomic). */
typedef struct {
	volatile uint64_t gen;
	volatile uint64_t seq;
	volatile uint64_t t_publish;
	volatile uint64_t vblank;
	uint64_t tick_us;
	uint64_t vblank_us;
	uint8_t pad[2048 - 48];
	volatile uint64_t client_scratch; /* offset 2048: written only by fence-rw */
} fence_page_t;

_Static_assert(sizeof(req_t) <= 64, "req_t must fit msg.i.raw");
_Static_assert(sizeof(resp_t) <= 64, "resp_t must fit msg.o.raw");
_Static_assert(sizeof(stats_t) <= 64, "stats_t must fit msg.o.raw");
_Static_assert(sizeof(fence_page_t) <= PAGE_SZ, "fence page is one page");


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

static inline uint64_t cnt_to_ns(uint64_t c)
{
	return (c * 1000u) / (cnt_freq / 1000000u);
}

static inline uint64_t us_to_cnt(uint64_t us)
{
	return us * (cnt_freq / 1000000u);
}

static inline double cnt_to_us(uint64_t c)
{
	return (double)cnt_to_ns(c) / 1000.0;
}


/* ------------------------------------------------------------ server state */

enum { K_FREE = 0, K_DELAY, K_ECHO_DEFER, K_WAIT, K_READ, K_POLL, K_HOLD, K_DUP };

typedef struct request {
	msg_t msg;      /* msgRecv'd directly here: a packed o.data points into THIS o.raw */
	msg_rid_t rid;
	uint64_t t_recv;
} request_t;

typedef struct {
	int kind;
	request_t *r;
	uint64_t deadline; /* cntvct; 0 = none */
	uint64_t target;   /* K_WAIT: fence seq */
	int file;          /* K_READ / K_POLL */
	resp_t dup;        /* K_DUP: the stale response to deliver */
	msg_rid_t duprid;
} slot_t;

typedef struct {
	int used;
	uint64_t last_seen;
} file_t;

static struct {
	uint32_t port;
	oid_t oid;
	handle_t lock;
	handle_t cond;  /* worker wake-up */
	handle_t qcond; /* main thread: quit */
	slot_t slots[MAX_SLOTS];
	unsigned int outstanding;
	file_t files[MAX_FILES];
	fence_page_t *fence;
	addr_t fence_pa;
	uint64_t tick_us, vblank_us;
	uint64_t seq, vblank, t_vblank;
	stats_t st;
	msg_rid_t stale_rid;
	resp_t stale_resp;
	volatile int quit;
} srv;


static void respond(request_t *r)
{
	(void)msgRespond(srv.port, &r->msg, r->rid);
	free(r);
}


static void set_resp(request_t *r, int err, uint64_t val, uint64_t val2)
{
	req_t *q = (req_t *)r->msg.i.raw;
	resp_t *p = (resp_t *)r->msg.o.raw;
	uint32_t token = q->token;

	memset(p, 0, sizeof(*p));
	p->err = err;
	p->token = token;
	p->rid = r->rid;
	p->t_recv = r->t_recv;
	p->val = val;
	p->val2 = val2;
	p->t_resp = now_cnt();
}


/* Park a request. Caller holds srv.lock. Returns -1 when full. */
static int _park(int kind, request_t *r, uint64_t deadline, uint64_t target, int file)
{
	unsigned int i;

	for (i = 0; i < MAX_SLOTS; i++) {
		if (srv.slots[i].kind == K_FREE) {
			srv.slots[i].kind = kind;
			srv.slots[i].r = r;
			srv.slots[i].deadline = deadline;
			srv.slots[i].target = target;
			srv.slots[i].file = file;
			if (r != NULL) {
				srv.outstanding++;
				if (srv.outstanding > srv.st.max_outstanding) {
					srv.st.max_outstanding = srv.outstanding;
				}
			}
			(void)condSignal(srv.cond);
			return 0;
		}
	}
	return -1;
}


/* Claim a slot (exactly-once: only the claimer responds). Caller holds lock. */
static request_t *_claim(slot_t *s)
{
	request_t *r = s->r;

	s->kind = K_FREE;
	s->r = NULL;
	if (r != NULL) {
		srv.outstanding--;
	}
	return r;
}


static uint64_t sum_words(const void *p, size_t n)
{
	const uint8_t *b = p;
	uint64_t s = 0;
	size_t i;

	for (i = 0; i < n; i++) {
		s = s * 31u + b[i];
	}
	return s;
}


static void fill_event(request_t *r, uint64_t seq, uint64_t tv, uint64_t missed)
{
	vbl_event_t ev;

	ev.seq = seq;
	ev.t_vblank = tv;
	ev.missed = missed;
	ev.t_resp = now_cnt();
	if ((r->msg.o.data != NULL) && (r->msg.o.size >= sizeof(ev))) {
		memcpy(r->msg.o.data, &ev, sizeof(ev));
		r->msg.o.err = sizeof(ev);
	}
	else {
		r->msg.o.err = -EINVAL;
	}
}


/* Worker: deadlines (OP_DELAY, OP_HOLD, K_DUP), WAIT and poll timeouts, handoffs. */
static void worker_thread(void *arg)
{
	(void)arg;
	request_t *todo[MAX_SLOTS];
	slot_t dups[8];
	unsigned int i, n, ndup;
	uint64_t now, next;
	time_t tmo;

	mutexLock(srv.lock);
	while (srv.quit == 0) {
		now = now_cnt();
		next = 0;
		n = 0;
		ndup = 0;
		for (i = 0; i < MAX_SLOTS; i++) {
			slot_t *s = &srv.slots[i];
			if ((s->kind == K_FREE) || (s->kind == K_READ)) {
				continue;
			}
			if ((s->kind == K_ECHO_DEFER) || ((s->deadline != 0) && (s->deadline <= now))) {
				if (s->kind == K_DUP) {
					if (ndup < 8) {
						dups[ndup++] = *s;
						s->kind = K_FREE;
					}
					continue;
				}
				if (s->kind == K_WAIT) {
					set_resp(s->r, -ETIMEDOUT, srv.seq, 0);
				}
				else if (s->kind == K_POLL) {
					s->r->msg.o.attr.val = 0;
					s->r->msg.o.err = EOK;
				}
				todo[n++] = _claim(s);
				continue;
			}
			if ((s->deadline != 0) && ((next == 0) || (s->deadline < next))) {
				next = s->deadline;
			}
		}
		mutexUnlock(srv.lock);

		for (i = 0; i < ndup; i++) {
			/* The racing-timeout shape: a late second response to a request
			 * that was already answered. Its rid now names somebody else. */
			msg_t m;
			memset(&m, 0, sizeof(m));
			m.type = mtDevCtl;
			memcpy(m.o.raw, &dups[i].dup, sizeof(dups[i].dup));
			srv.st.dup_respond_rc = msgRespond(srv.port, &m, dups[i].duprid);
		}

		for (i = 0; i < n; i++) {
			request_t *r = todo[i];
			if (r->msg.type == mtDevCtl) {
				req_t *q = (req_t *)r->msg.i.raw;
				if ((q->op == OP_DELAY) || (q->op == OP_ECHO_DEFER) || (q->op == OP_HOLD)) {
					uint64_t isum = 0;
					if ((r->msg.i.data != NULL) && (r->msg.i.size != 0)) {
						isum = sum_words(r->msg.i.data, r->msg.i.size);
					}
					if ((r->msg.o.data != NULL) && (r->msg.o.size != 0)) {
						memset(r->msg.o.data, (int)(q->token & 0xff), r->msg.o.size);
					}
					set_resp(r, 0, isum, 0);
					if (q->op == OP_HOLD) {
						int rc = msgRespond(srv.port, &r->msg, r->rid);
						srv.st.late_respond_rc = rc;
						free(r);
						continue;
					}
				}
			}
			respond(r);
		}

		mutexLock(srv.lock);
		if (n != 0 || ndup != 0) {
			continue;
		}
		if (next == 0) {
			tmo = 0;
		}
		else {
			now = now_cnt();
			tmo = (next > now) ? (time_t)(cnt_to_ns(next - now) / 1000u) + 1 : 1;
		}
		(void)condWait(srv.cond, srv.lock, tmo);
	}
	mutexUnlock(srv.lock);
	endthread();
}


/* "IRQ" thread: the fence/vblank producer. Responds to WAIT, READ and POLL
 * requests itself -- the thread-that-did-not-receive-it case. */
static void irq_thread(void *arg)
{
	(void)arg;
	request_t *todo[MAX_SLOTS];
	unsigned int i, n;
	uint64_t tick = us_to_cnt(srv.tick_us);
	uint64_t vbl = us_to_cnt(srv.vblank_us);
	uint64_t next_tick = now_cnt() + tick;
	uint64_t next_vbl = now_cnt() + vbl;
	uint64_t now, t;
	int is_vbl;

	while (srv.quit == 0) {
		now = now_cnt();
		if (now < next_tick) {
			(void)usleep((useconds_t)(cnt_to_ns(next_tick - now) / 1000u));
			continue;
		}
		while (now >= next_tick + tick) { /* fell behind: count, don't burst */
			next_tick += tick;
			srv.st.ticks_skipped++;
		}
		next_tick += tick;
		is_vbl = (now >= next_vbl) ? 1 : 0;
		if (is_vbl != 0) {
			while (now >= next_vbl) {
				next_vbl += vbl;
			}
		}

		mutexLock(srv.lock);
		srv.seq++;
		t = now_cnt();
		if (is_vbl != 0) {
			srv.vblank++;
			srv.t_vblank = t;
		}
		/* publish */
		srv.fence->gen++;
		__asm__ volatile("dmb ishst" ::: "memory");
		srv.fence->seq = srv.seq;
		srv.fence->t_publish = t;
		srv.fence->vblank = srv.vblank;
		__asm__ volatile("dmb ishst" ::: "memory");
		srv.fence->gen++;

		n = 0;
		for (i = 0; i < MAX_SLOTS; i++) {
			slot_t *s = &srv.slots[i];
			if ((s->kind == K_WAIT) && (srv.seq >= s->target)) {
				set_resp(s->r, 0, srv.seq, 0);
				((resp_t *)s->r->msg.o.raw)->t_resp = t; /* publish time */
				todo[n++] = _claim(s);
			}
			else if ((is_vbl != 0) && (s->kind == K_READ)) {
				file_t *f = &srv.files[s->file];
				fill_event(s->r, srv.vblank, t, (srv.vblank > f->last_seen + 1) ? srv.vblank - f->last_seen - 1 : 0);
				f->last_seen = srv.vblank;
				todo[n++] = _claim(s);
			}
			else if ((is_vbl != 0) && (s->kind == K_POLL)) {
				s->r->msg.o.attr.val = POLLIN;
				s->r->msg.o.err = EOK;
				todo[n++] = _claim(s);
			}
		}
		srv.st.responds_from_irq += n;
		mutexUnlock(srv.lock);

		for (i = 0; i < n; i++) {
			respond(todo[i]);
		}
	}
	endthread();
}


static int file_alloc(void)
{
	int i;

	for (i = 1; i < MAX_FILES; i++) {
		if (srv.files[i].used == 0) {
			srv.files[i].used = 1;
			srv.files[i].last_seen = srv.vblank;
			return i;
		}
	}
	return -ENFILE;
}


/* Returns 1 if the request was parked (someone else responds). */
static int handle_devctl(request_t *r)
{
	req_t *q = (req_t *)r->msg.i.raw;
	uint64_t now = now_cnt();
	int parked = 0;

	if (q->magic != REQ_MAGIC) {
		set_resp(r, -EINVAL, 0, 0);
		return 0;
	}

	switch (q->op) {
		case OP_ECHO:
			if ((r->msg.i.data != NULL) && (r->msg.i.size != 0)) {
				(void)sum_words(r->msg.i.data, r->msg.i.size);
			}
			if ((r->msg.o.data != NULL) && (r->msg.o.size != 0)) {
				memset(r->msg.o.data, 0x5a, r->msg.o.size);
			}
			set_resp(r, 0, 0, 0);
			break;

		case OP_ECHO_DEFER:
		case OP_DELAY:
		case OP_HOLD:
			mutexLock(srv.lock);
			if ((q->op == OP_HOLD) && (r->rid == srv.stale_rid)) {
				/* Simulate a racing path answering the OLD request 50 ms from
				 * now; the kernel will deliver it to THIS request. */
				unsigned int i;
				for (i = 0; i < MAX_SLOTS; i++) {
					if (srv.slots[i].kind == K_FREE) {
						srv.slots[i].kind = K_DUP;
						srv.slots[i].r = NULL;
						srv.slots[i].deadline = now + us_to_cnt(50000);
						srv.slots[i].dup = srv.stale_resp;
						srv.slots[i].duprid = srv.stale_rid;
						break;
					}
				}
				srv.stale_rid = -1;
			}
			parked = (_park((q->op == OP_ECHO_DEFER) ? K_ECHO_DEFER : ((q->op == OP_HOLD) ? K_HOLD : K_DELAY),
				r, (q->op == OP_ECHO_DEFER) ? 0 : now + us_to_cnt((uint64_t)q->delay_ms * 1000u), 0, 0) == 0);
			mutexUnlock(srv.lock);
			if (parked == 0) {
				set_resp(r, -ENOSPC, 0, 0);
			}
			break;

		case OP_WAIT:
			mutexLock(srv.lock);
			if (srv.seq >= q->arg) {
				set_resp(r, 0, srv.seq, 1); /* already signalled: val2=1 */
			}
			else {
				parked = (_park(K_WAIT, r, (q->delay_ms != 0) ? now + us_to_cnt((uint64_t)q->delay_ms * 1000u) : 0,
					q->arg, 0) == 0);
				if (parked == 0) {
					set_resp(r, -ENOSPC, 0, 0);
				}
			}
			mutexUnlock(srv.lock);
			break;

		case OP_FENCE_INFO:
			set_resp(r, 0, (uint64_t)srv.fence_pa, srv.tick_us);
			((resp_t *)r->msg.o.raw)->flags = (uint32_t)srv.vblank_us;
			break;

		case OP_FENCE_PEEK:
			set_resp(r, 0, srv.fence->client_scratch, 0);
			break;

		case OP_STATS: {
			stats_t s;
			mutexLock(srv.lock);
			s = srv.st;
			mutexUnlock(srv.lock);
			memcpy(r->msg.o.raw, &s, sizeof(s));
			break;
		}

		case OP_STALE:
			set_resp(r, 0, 0, 0);
			mutexLock(srv.lock);
			srv.stale_rid = r->rid;
			srv.stale_resp = *(resp_t *)r->msg.o.raw;
			srv.stale_resp.flags = 0x57a1e; /* marks the duplicate */
			mutexUnlock(srv.lock);
			break;

		case OP_BADRID: {
			msg_t m;
			int rc;
			memset(&m, 0, sizeof(m));
			m.type = mtDevCtl;
			rc = msgRespond(srv.port, &m, 0x7ffffff0);
			set_resp(r, 0, (uint64_t)(int64_t)rc, 0);
			break;
		}

		case OP_QUIT:
			/* Respond BEFORE the process can exit: a request that is still
			 * unanswered when the port dies leaves its sender blocked forever. */
			set_resp(r, 0, 0, 0);
			respond(r);
			mutexLock(srv.lock);
			srv.quit = 1;
			(void)condBroadcast(srv.cond);
			(void)condSignal(srv.qcond);
			mutexUnlock(srv.lock);
			parked = 1;
			break;

		default:
			set_resp(r, -ENOSYS, 0, 0);
			break;
	}

	return parked;
}


static int handle(request_t *r)
{
	int id = (int)r->msg.oid.id;
	file_t *f = ((id > 0) && (id < MAX_FILES) && (srv.files[id].used != 0)) ? &srv.files[id] : NULL;
	int parked = 0;

	switch (r->msg.type) {
		case mtOpen:
			mutexLock(srv.lock);
			r->msg.o.err = file_alloc(); /* > 0: kernel makes it this fd's oid.id */
			mutexUnlock(srv.lock);
			break;

		case mtClose:
			mutexLock(srv.lock);
			if (f != NULL) {
				f->used = 0;
			}
			mutexUnlock(srv.lock);
			r->msg.o.err = EOK;
			break;

		case mtRead:
			if (f == NULL) {
				r->msg.o.err = -EBADF;
				break;
			}
			mutexLock(srv.lock);
			if (srv.vblank > f->last_seen) {
				fill_event(r, srv.vblank, srv.t_vblank, srv.vblank - f->last_seen - 1);
				f->last_seen = srv.vblank;
			}
			else {
				parked = (_park(K_READ, r, 0, 0, id) == 0);
				if (parked == 0) {
					r->msg.o.err = -ENOSPC;
				}
			}
			mutexUnlock(srv.lock);
			break;

		case mtGetAttr:
			if (r->msg.i.attr.type != atPollStatus) {
				r->msg.o.err = -EINVAL;
				break;
			}
			{
				unsigned long long v = (unsigned long long)r->msg.i.attr.val;
				unsigned int block_ms = (unsigned int)(v >> 16);

				mutexLock(srv.lock);
				srv.st.pollstatus_msgs++;
				if (block_ms != 0) {
					srv.st.pollstatus_with_block++;
				}
				if ((f != NULL) && (srv.vblank > f->last_seen)) {
					r->msg.o.attr.val = POLLIN;
					r->msg.o.err = EOK;
				}
				else if ((f != NULL) && (block_ms != 0)) {
					/* The block_ms contract (lwip port/sockets.c): hold the
					 * reply until ready or block_ms elapses. */
					parked = (_park(K_POLL, r, now_cnt() + us_to_cnt((uint64_t)block_ms * 1000u), 0, id) == 0);
					if (parked == 0) {
						r->msg.o.attr.val = 0;
						r->msg.o.err = EOK;
					}
				}
				else {
					r->msg.o.attr.val = 0;
					r->msg.o.err = EOK;
				}
				mutexUnlock(srv.lock);
			}
			break;

		case mtDevCtl:
			parked = handle_devctl(r);
			break;

		default:
			r->msg.o.err = -ENOSYS;
			break;
	}

	return parked;
}


static void recv_thread(void *arg)
{
	(void)arg;
	request_t *r;

	for (;;) {
		r = malloc(sizeof(*r));
		if (r == NULL) {
			(void)usleep(1000);
			continue;
		}
		if (msgRecv(srv.port, &r->msg, &r->rid) < 0) {
			free(r);
			continue;
		}
		r->t_recv = now_cnt();

		mutexLock(srv.lock);
		if (srv.outstanding != 0) {
			srv.st.recv_while_outstanding++;
		}
		mutexUnlock(srv.lock);

		if (handle(r) == 0) {
			respond(r);
		}
	}
}


static int start_thread(void (*fn)(void *), int prio)
{
	void *stack = malloc(STACK_SZ);

	if (stack == NULL) {
		return -ENOMEM;
	}
	return beginthreadex(fn, prio, stack, STACK_SZ, NULL, NULL);
}


static int server_main(int argc, char **argv)
{
	int nrecv = 1, i, opt;
	void *page;

	srv.tick_us = 1000;
	srv.vblank_us = 16667;
	srv.stale_rid = -1;
	srv.st.dup_respond_rc = 1;  /* sentinel: no duplicate response was sent */
	srv.st.late_respond_rc = 1; /* sentinel: no OP_HOLD was answered */

	optind = 2;
	while ((opt = getopt(argc, argv, "r:t:v:")) != -1) {
		switch (opt) {
			case 'r': nrecv = atoi(optarg); break;
			case 't': srv.tick_us = (uint64_t)atoi(optarg); break;
			case 'v': srv.vblank_us = (uint64_t)atoi(optarg); break;
			default:
				fprintf(stderr, "usage: ipcprobe server [-r recv_threads] [-t tick_us] [-v vblank_us]\n");
				return 2;
		}
	}
	if ((nrecv < 1) || (nrecv > 8) || (srv.tick_us < 100) || (srv.vblank_us < srv.tick_us)) {
		fprintf(stderr, "ipcprobe: bad server arguments\n");
		return 2;
	}

	/* The fence page: MAP_CONTIGUOUS gives an object-backed page whose PA
	 * cannot change under us (an anonymous amap page could be COW-replaced if
	 * this process ever forked). Cached, like the client's MAP_PHYSMEM view:
	 * one memory type for one PA. Written before va2pa() -- an untouched
	 * page has no PA yet. */
	page = mmap(NULL, PAGE_SZ, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_CONTIGUOUS, -1, 0);
	if (page == MAP_FAILED) {
		printf("IPCPROBE server FAIL mmap fence page errno=%d\n", errno);
		return 1;
	}
	memset(page, 0, PAGE_SZ);
	srv.fence = page;
	srv.fence->tick_us = srv.tick_us;
	srv.fence->vblank_us = srv.vblank_us;
	srv.fence_pa = va2pa(page);

	if ((mutexCreate(&srv.lock) < 0) || (condCreate(&srv.cond) < 0) || (condCreate(&srv.qcond) < 0)) {
		printf("IPCPROBE server FAIL mutex/cond\n");
		return 1;
	}
	if (portCreate(&srv.port) < 0) {
		printf("IPCPROBE server FAIL portCreate\n");
		return 1;
	}
	srv.oid.port = srv.port;
	srv.oid.id = 0;
	if (create_dev(&srv.oid, DEV_PATH) < 0) {
		printf("IPCPROBE server FAIL create_dev %s\n", DEV_PATH);
		return 1;
	}

	/* irq thread above normal priority, like an interrupt service thread */
	if ((start_thread(irq_thread, 2) < 0) || (start_thread(worker_thread, 3) < 0)) {
		printf("IPCPROBE server FAIL threads\n");
		return 1;
	}
	printf("IPCPROBE server ready port=%u dev=%s fence_pa=0x%llx tick_us=%llu vblank_us=%llu recv_threads=%d cntfrq=%llu\n",
		srv.port, DEV_PATH, (unsigned long long)srv.fence_pa, (unsigned long long)srv.tick_us,
		(unsigned long long)srv.vblank_us, nrecv, (unsigned long long)cnt_freq);
	fflush(stdout);

	for (i = 0; i < nrecv; i++) {
		if (start_thread(recv_thread, 4) < 0) {
			printf("IPCPROBE server FAIL recv thread\n");
			return 1;
		}
	}

	/* The main thread only waits for OP_QUIT. Receivers blocked in msgRecv
	 * die with the process. */
	mutexLock(srv.lock);
	while (srv.quit == 0) {
		(void)condWait(srv.qcond, srv.lock, 0);
	}
	mutexUnlock(srv.lock);
	return 0;
}


static void server_quit_watch(void)
{
	/* Answer every parked request before exiting: a received-but-unanswered
	 * rid is never rejected by the kernel when the port dies (msg.c:518-527),
	 * so its client thread would wait forever, uninterruptibly. */
	request_t *todo[MAX_SLOTS];
	unsigned int i, n = 0;

	mutexLock(srv.lock);
	for (i = 0; i < MAX_SLOTS; i++) {
		slot_t *s = &srv.slots[i];
		if ((s->kind != K_FREE) && (s->r != NULL)) {
			request_t *r = s->r;
			if (r->msg.type == mtDevCtl) {
				set_resp(r, -ECANCELED, 0, 0);
			}
			else if (r->msg.type == mtGetAttr) {
				r->msg.o.attr.val = POLLHUP;
				r->msg.o.err = EOK;
			}
			else {
				r->msg.o.err = -ECANCELED;
			}
			todo[n++] = _claim(s);
		}
		else {
			s->kind = K_FREE;
		}
	}
	mutexUnlock(srv.lock);
	for (i = 0; i < n; i++) {
		respond(todo[i]);
	}
	/* The irq/worker threads may hold requests they claimed just before the
	 * quit; give their msgRespond calls time to land before the port dies. */
	(void)usleep(50000);
	printf("IPCPROBE server quit cancelled=%u max_outstanding=%llu responds_from_irq=%llu\n", n,
		(unsigned long long)srv.st.max_outstanding, (unsigned long long)srv.st.responds_from_irq);
	fflush(stdout);
}


/* ------------------------------------------------------------------ client */

static oid_t dev_oid;

static int client_resolve(void)
{
	int i;

	for (i = 0; i < 50; i++) {
		if (lookup(DEV_PATH, NULL, &dev_oid) == 0) {
			return 0;
		}
		(void)usleep(100000);
	}
	printf("IPCPROBE client FAIL cannot resolve %s (is 'ipcprobe server &' running?)\n", DEV_PATH);
	return -1;
}


static int call(uint32_t op, uint32_t token, uint32_t delay_ms, uint64_t arg,
	const void *idata, size_t isize, void *odata, size_t osize, resp_t *out)
{
	msg_t msg;
	req_t *q = (req_t *)msg.i.raw;
	int err;

	memset(&msg, 0, sizeof(msg));
	msg.type = mtDevCtl;
	msg.oid = dev_oid;
	q->magic = REQ_MAGIC;
	q->op = op;
	q->token = token;
	q->delay_ms = delay_ms;
	q->arg = arg;
	q->t_send = now_cnt();
	msg.i.data = idata;
	msg.i.size = isize;
	msg.o.data = odata;
	msg.o.size = osize;

	err = msgSend(dev_oid.port, &msg);
	if (out != NULL) {
		memcpy(out, msg.o.raw, sizeof(*out));
	}
	if (err < 0) {
		return err;
	}
	return ((resp_t *)msg.o.raw)->err;
}


static int get_stats(stats_t *s)
{
	msg_t msg;
	req_t *q = (req_t *)msg.i.raw;
	int err;

	memset(&msg, 0, sizeof(msg));
	msg.type = mtDevCtl;
	msg.oid = dev_oid;
	q->magic = REQ_MAGIC;
	q->op = OP_STATS;
	err = msgSend(dev_oid.port, &msg);
	memcpy(s, msg.o.raw, sizeof(*s));
	return err;
}


static int cmp_u64(const void *a, const void *b)
{
	uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
	return (x < y) ? -1 : ((x > y) ? 1 : 0);
}


typedef struct {
	double p50, p90, p99, max, mean;
} dist_t;

/* v[] in cntvct ticks; sorts v. */
static dist_t dist(uint64_t *v, size_t n)
{
	dist_t d;
	uint64_t sum = 0;
	size_t i;

	memset(&d, 0, sizeof(d));
	if (n == 0) {
		return d;
	}
	qsort(v, n, sizeof(*v), cmp_u64);
	for (i = 0; i < n; i++) {
		sum += v[i];
	}
	d.p50 = cnt_to_us(v[n / 2]);
	d.p90 = cnt_to_us(v[(n * 90) / 100]);
	d.p99 = cnt_to_us(v[(n * 99) / 100]);
	d.max = cnt_to_us(v[n - 1]);
	d.mean = cnt_to_us(sum / n);
	return d;
}


/* ---- rtt */

static volatile int hog_stop;

static volatile uint64_t hog_count;

static void *hog_fn(void *arg)
{
	(void)arg;
	while (hog_stop == 0) {
		hog_count++;
	}
	return NULL;
}


static void rtt_variant(const char *name, uint32_t op, const void *idata, size_t isize, void *odata, size_t osize, int n)
{
	uint64_t *v = malloc(sizeof(uint64_t) * (size_t)n);
	uint64_t *srvside = malloc(sizeof(uint64_t) * (size_t)n);
	int i, errs = 0;
	resp_t r;
	dist_t d, ds;

	if ((v == NULL) || (srvside == NULL)) {
		printf("IPCPROBE rtt %s FAIL oom\n", name);
		free(v);
		free(srvside);
		return;
	}
	for (i = 0; i < 50; i++) { /* warm-up */
		(void)call(op, 0, 0, 0, idata, isize, odata, osize, &r);
	}
	for (i = 0; i < n; i++) {
		uint64_t t0 = now_cnt();
		if (call(op, (uint32_t)i, 0, 0, idata, isize, odata, osize, &r) != 0) {
			errs++;
		}
		v[i] = now_cnt() - t0;
		srvside[i] = (r.t_resp > r.t_recv) ? r.t_resp - r.t_recv : 0;
	}
	d = dist(v, (size_t)n);
	ds = dist(srvside, (size_t)n);
	printf("IPCPROBE rtt %s n=%d errs=%d p50_us=%.1f p90_us=%.1f p99_us=%.1f max_us=%.1f mean_us=%.1f srv_p50_us=%.1f\n",
		name, n, errs, d.p50, d.p90, d.p99, d.max, d.mean, ds.p50);
	fflush(stdout);
	free(v);
	free(srvside);
}


static int test_rtt(void)
{
	const int n = 5000;
	uint8_t *ibuf, *obuf;
	pthread_t hog[3];
	int i;

	/* one page-aligned buffer each, plus room for an unaligned view */
	ibuf = mmap(NULL, 2 * PAGE_SZ, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, -1, 0);
	obuf = mmap(NULL, 2 * PAGE_SZ, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, -1, 0);
	if ((ibuf == MAP_FAILED) || (obuf == MAP_FAILED)) {
		printf("IPCPROBE rtt FAIL mmap\n");
		return 1;
	}
	memset(ibuf, 0x11, 2 * PAGE_SZ);
	memset(obuf, 0x22, 2 * PAGE_SZ);

	rtt_variant("small", OP_ECHO, NULL, 0, NULL, 0, n);
	/* the shape of an ioctl()-style call: a small struct by pointer, not in
	 * i.raw -- one shadow page each way (msg.c:111-137) */
	rtt_variant("i64_ptr", OP_ECHO, ibuf + 16, 64, NULL, 0, n);
	rtt_variant("io64_ptr", OP_ECHO, ibuf + 16, 64, obuf + 16, 64, n);
	rtt_variant("i4k_aligned", OP_ECHO, ibuf, PAYLOAD_SZ, NULL, 0, n);
	rtt_variant("i4k_unaligned", OP_ECHO, ibuf + 16, PAYLOAD_SZ, NULL, 0, n);
	rtt_variant("o4k_aligned", OP_ECHO, NULL, 0, obuf, PAYLOAD_SZ, n);
	rtt_variant("o4k_unaligned", OP_ECHO, NULL, 0, obuf + 16, PAYLOAD_SZ, n);
	rtt_variant("io4k_aligned", OP_ECHO, ibuf, PAYLOAD_SZ, obuf, PAYLOAD_SZ, n);
	rtt_variant("small_handoff", OP_ECHO_DEFER, NULL, 0, NULL, 0, n);

	/* Same-core vs cross-core is not controllable (no affinity syscall);
	 * this is the "other cores busy" proxy: 3 spinners at our priority. */
	hog_stop = 0;
	for (i = 0; i < 3; i++) {
		(void)pthread_create(&hog[i], NULL, hog_fn, NULL);
	}
	rtt_variant("small_hog3", OP_ECHO, NULL, 0, NULL, 0, n / 10);
	rtt_variant("i4k_aligned_hog3", OP_ECHO, ibuf, PAYLOAD_SZ, NULL, 0, n / 10);
	hog_stop = 1;
	for (i = 0; i < 3; i++) {
		(void)pthread_join(hog[i], NULL);
	}

	(void)munmap(ibuf, 2 * PAGE_SZ);
	(void)munmap(obuf, 2 * PAGE_SZ);
	return 0;
}


/* ---- deferred */

#define DEF_THREADS 4
#define DEF_PER     8

typedef struct {
	int idx;
	int ok, bad_token, bad_err, early, late, odata_bad, idata_bad;
	uint64_t max_delay_ms, max_late_ms;
} def_ctx_t;

static const uint32_t def_delays[] = { 5, 20, 50, 100, 200, 10, 150, 30 };

static void *deferred_fn(void *arg)
{
	def_ctx_t *c = arg;
	uint8_t *ibuf = mmap(NULL, 2 * PAGE_SZ, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, -1, 0);
	uint8_t *obuf = mmap(NULL, 2 * PAGE_SZ, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, -1, 0);
	int k;

	if ((ibuf == MAP_FAILED) || (obuf == MAP_FAILED)) {
		c->bad_err = DEF_PER;
		return NULL;
	}
	for (k = 0; k < DEF_PER; k++) {
		uint32_t token = (uint32_t)(c->idx * 1000 + k + 1);
		uint32_t delay = def_delays[(c->idx + k) % 8];
		int payload = (k & 1); /* odd requests carry 4 KiB each way, unaligned */
		uint8_t *ip = ibuf + 24, *op = obuf + 40;
		uint64_t t0, el_ms, isum = 0;
		resp_t r;
		int rc, j;

		if (payload != 0) {
			for (j = 0; j < (int)PAYLOAD_SZ; j++) {
				ip[j] = (uint8_t)(token + (uint32_t)j);
			}
			isum = sum_words(ip, PAYLOAD_SZ);
			memset(op, 0, PAYLOAD_SZ);
		}
		t0 = now_cnt();
		rc = call(OP_DELAY, token, delay, 0, payload ? ip : NULL, payload ? PAYLOAD_SZ : 0,
			payload ? op : NULL, payload ? PAYLOAD_SZ : 0, &r);
		el_ms = cnt_to_ns(now_cnt() - t0) / 1000000u;

		if (rc != 0) {
			c->bad_err++;
			continue;
		}
		if (r.token != token) {
			c->bad_token++;
			continue;
		}
		if (el_ms + 1 < delay) {
			c->early++;
		}
		if (el_ms > delay + 50) {
			c->late++;
		}
		if (el_ms > c->max_delay_ms) {
			c->max_delay_ms = el_ms;
		}
		if ((el_ms > delay) && (el_ms - delay > c->max_late_ms)) {
			c->max_late_ms = el_ms - delay;
		}
		if (payload != 0) {
			if (r.val != isum) {
				c->idata_bad++;
			}
			for (j = 0; j < (int)PAYLOAD_SZ; j++) {
				if (op[j] != (uint8_t)(token & 0xff)) {
					c->odata_bad++;
					break;
				}
			}
		}
		c->ok++;
	}
	(void)munmap(ibuf, 2 * PAGE_SZ);
	(void)munmap(obuf, 2 * PAGE_SZ);
	return NULL;
}


typedef struct {
	uint32_t delay;
	uint64_t t_done;
	int rc;
} ooo_ctx_t;

static void *ooo_fn(void *arg)
{
	ooo_ctx_t *c = arg;
	resp_t r;

	c->rc = call(OP_DELAY, c->delay, c->delay, 0, NULL, 0, NULL, 0, &r);
	c->t_done = now_cnt();
	return NULL;
}


static int test_deferred(void)
{
	pthread_t th[DEF_THREADS];
	def_ctx_t ctx[DEF_THREADS];
	ooo_ctx_t slow, fast;
	pthread_t ts, tf;
	stats_t s0, s1;
	int i, ok = 0, total = DEF_THREADS * DEF_PER, bt = 0, be = 0, early = 0, late = 0, ob = 0, ib = 0, ooo;
	uint64_t maxd = 0, maxl = 0;

	(void)get_stats(&s0);
	memset(ctx, 0, sizeof(ctx));
	for (i = 0; i < DEF_THREADS; i++) {
		ctx[i].idx = i;
		(void)pthread_create(&th[i], NULL, deferred_fn, &ctx[i]);
	}
	for (i = 0; i < DEF_THREADS; i++) {
		(void)pthread_join(th[i], NULL);
		ok += ctx[i].ok;
		bt += ctx[i].bad_token;
		be += ctx[i].bad_err;
		early += ctx[i].early;
		late += ctx[i].late;
		ob += ctx[i].odata_bad;
		ib += ctx[i].idata_bad;
		if (ctx[i].max_delay_ms > maxd) {
			maxd = ctx[i].max_delay_ms;
		}
		if (ctx[i].max_late_ms > maxl) {
			maxl = ctx[i].max_late_ms;
		}
	}

	/* Out-of-order completion: a 300 ms request sent first, a 20 ms one 30 ms
	 * later from another thread; the second must come back first. */
	memset(&slow, 0, sizeof(slow));
	memset(&fast, 0, sizeof(fast));
	slow.delay = 300;
	fast.delay = 20;
	(void)pthread_create(&ts, NULL, ooo_fn, &slow);
	(void)usleep(30000);
	(void)pthread_create(&tf, NULL, ooo_fn, &fast);
	(void)pthread_join(ts, NULL);
	(void)pthread_join(tf, NULL);
	ooo = ((slow.rc == 0) && (fast.rc == 0) && (fast.t_done < slow.t_done)) ? 1 : 0;

	(void)get_stats(&s1);
	printf("IPCPROBE deferred ok=%d/%d bad_token=%d bad_err=%d early=%d late=%d odata_bad=%d idata_bad=%d "
		"max_delay_ms=%llu max_late_ms=%llu ooo=%d outstanding_max=%llu recv_while_outstanding=%llu\n",
		ok, total, bt, be, early, late, ob, ib, (unsigned long long)maxd, (unsigned long long)maxl, ooo,
		(unsigned long long)s1.max_outstanding,
		(unsigned long long)(s1.recv_while_outstanding - s0.recv_while_outstanding));
	fflush(stdout);
	return ((ok == total) && (ooo == 1) && (ob == 0) && (ib == 0)) ? 0 : 1;
}


/* ---- timeout */

typedef struct {
	int rc;
	uint64_t el_ms;
} tmo_ctx_t;

static void *timeout_fn(void *arg)
{
	tmo_ctx_t *c = arg;
	uint64_t t0 = now_cnt();
	resp_t r;

	c->rc = call(OP_WAIT, 1, 100, (uint64_t)1 << 62, NULL, 0, NULL, 0, &r); /* never signals */
	c->el_ms = cnt_to_ns(now_cnt() - t0) / 1000000u;
	return NULL;
}


static int test_timeout(void)
{
	pthread_t th[4];
	tmo_ctx_t c[4];
	int i, ok = 0;
	uint64_t mn = ~0ull, mx = 0;

	memset(c, 0, sizeof(c));
	for (i = 0; i < 4; i++) {
		(void)pthread_create(&th[i], NULL, timeout_fn, &c[i]);
	}
	for (i = 0; i < 4; i++) {
		(void)pthread_join(th[i], NULL);
		if ((c[i].rc == -ETIMEDOUT) && (c[i].el_ms >= 99)) {
			ok++;
		}
		mn = (c[i].el_ms < mn) ? c[i].el_ms : mn;
		mx = (c[i].el_ms > mx) ? c[i].el_ms : mx;
	}
	printf("IPCPROBE timeout ok=%d/4 rc0=%d want=%d elapsed_ms_min=%llu elapsed_ms_max=%llu\n", ok, c[0].rc, -ETIMEDOUT,
		(unsigned long long)mn, (unsigned long long)mx);
	fflush(stdout);
	return (ok == 4) ? 0 : 1;
}


/* ---- fence page mapping (shared by wait/fence tests) */

static volatile const fence_page_t *fence_map(int writable, addr_t *pa_out)
{
	resp_t r;
	void *p;

	if (call(OP_FENCE_INFO, 0, 0, 0, NULL, 0, NULL, 0, &r) != 0) {
		return NULL;
	}
	if (pa_out != NULL) {
		*pa_out = (addr_t)r.val;
	}
	/* Cached, to match the server's view of the same PA. */
	p = mmap(NULL, PAGE_SZ, PROT_READ | (writable ? PROT_WRITE : 0), MAP_PHYSMEM | MAP_ANONYMOUS, -1, (off_t)r.val);
	return (p == MAP_FAILED) ? NULL : p;
}


/* seqlock read; returns retries */
static unsigned int fence_read(volatile const fence_page_t *f, uint64_t *seq, uint64_t *t)
{
	unsigned int retries = 0;
	uint64_t g1, g2;

	for (;;) {
		g1 = f->gen;
		__asm__ volatile("dmb ishld" ::: "memory");
		*seq = f->seq;
		*t = f->t_publish;
		__asm__ volatile("dmb ishld" ::: "memory");
		g2 = f->gen;
		if (((g1 & 1u) == 0) && (g1 == g2)) {
			return retries;
		}
		retries++;
	}
}


/* ---- wait (fence slow path: deferred reply from the irq thread) */

#define WAIT_THREADS 4
#define WAIT_PER     50

typedef struct {
	volatile const fence_page_t *f;
	uint64_t lat[WAIT_PER];
	int n, ok, errs, fast, early;
} wait_ctx_t;

static void *wait_fn(void *arg)
{
	wait_ctx_t *c = arg;
	int k;

	for (k = 0; k < WAIT_PER; k++) {
		uint64_t seq, t, target, now;
		resp_t r;
		int rc;

		(void)fence_read(c->f, &seq, &t);
		target = seq + 3u;
		rc = call(OP_WAIT, (uint32_t)k, 1000, target, NULL, 0, NULL, 0, &r);
		now = now_cnt();
		if (rc != 0) {
			c->errs++;
			continue;
		}
		if (r.val < target) {
			c->early++;
		}
		if (r.val2 == 1) {
			c->fast++; /* already signalled when the server looked */
		}
		else if (now > r.t_resp) {
			c->lat[c->n++] = now - r.t_resp;
		}
		c->ok++;
	}
	return NULL;
}


static int test_wait(void)
{
	pthread_t th[WAIT_THREADS];
	wait_ctx_t *c = calloc(WAIT_THREADS, sizeof(*c));
	uint64_t *all;
	volatile const fence_page_t *f;
	int i, j, n = 0, ok = 0, errs = 0, fast = 0, early = 0;
	dist_t d;

	f = fence_map(0, NULL);
	if ((c == NULL) || (f == NULL)) {
		printf("IPCPROBE wait FAIL fence map / oom\n");
		free(c);
		return 1;
	}
	all = malloc(sizeof(uint64_t) * WAIT_THREADS * WAIT_PER);
	for (i = 0; i < WAIT_THREADS; i++) {
		c[i].f = f;
		(void)pthread_create(&th[i], NULL, wait_fn, &c[i]);
	}
	for (i = 0; i < WAIT_THREADS; i++) {
		(void)pthread_join(th[i], NULL);
		ok += c[i].ok;
		errs += c[i].errs;
		fast += c[i].fast;
		early += c[i].early;
		for (j = 0; j < c[i].n; j++) {
			all[n++] = c[i].lat[j];
		}
	}
	d = dist(all, (size_t)n);
	printf("IPCPROBE wait ok=%d/%d errs=%d early=%d already_signalled=%d wake_n=%d wake_p50_us=%.1f wake_p90_us=%.1f "
		"wake_p99_us=%.1f wake_max_us=%.1f\n",
		ok, WAIT_THREADS * WAIT_PER, errs, early, fast, n, d.p50, d.p90, d.p99, d.max);
	fflush(stdout);
	(void)munmap((void *)f, PAGE_SZ);
	free(all);
	free(c);
	return ((errs == 0) && (early == 0)) ? 0 : 1;
}


/* ---- read (blocking event read, deferred mtRead) */

static int test_read(void)
{
	const int n = 60;
	uint64_t lat[60];
	uint64_t prev = 0, missed = 0;
	int fd, i, errs = 0, got = 0;
	vbl_event_t ev;
	dist_t d;

	fd = open(DEV_PATH, O_RDWR);
	if (fd < 0) {
		printf("IPCPROBE read FAIL open errno=%d\n", errno);
		return 1;
	}
	/* drain whatever is pending, then time n events */
	(void)read(fd, &ev, sizeof(ev));
	for (i = 0; i < n; i++) {
		ssize_t rc = read(fd, &ev, sizeof(ev));
		uint64_t now = now_cnt();
		if (rc != (ssize_t)sizeof(ev)) {
			errs++;
			continue;
		}
		if ((prev != 0) && (ev.seq != prev + 1)) {
			missed += ev.seq - prev - 1;
		}
		prev = ev.seq;
		lat[got++] = (now > ev.t_vblank) ? now - ev.t_vblank : 0;
	}
	(void)close(fd);
	d = dist(lat, (size_t)got);
	printf("IPCPROBE read events=%d errs=%d missed=%llu wake_p50_us=%.1f wake_p90_us=%.1f wake_p99_us=%.1f wake_max_us=%.1f\n",
		got, errs, (unsigned long long)missed, d.p50, d.p90, d.p99, d.max);
	fflush(stdout);
	return (errs == 0) ? 0 : 1;
}


/* ---- poll */

static int test_poll(void)
{
	const int n = 60;
	uint64_t lat[60];
	int fd, i, errs = 0, got = 0, timeouts = 0;
	stats_t s0, s1;
	vbl_event_t ev;
	dist_t d;
	uint64_t msgs, blk;

	fd = open(DEV_PATH, O_RDWR);
	if (fd < 0) {
		printf("IPCPROBE poll FAIL open errno=%d\n", errno);
		return 1;
	}
	(void)read(fd, &ev, sizeof(ev)); /* consume: next poll must wait for a new vblank */
	(void)get_stats(&s0);
	for (i = 0; i < n; i++) {
		struct pollfd p;
		int rc;
		uint64_t now;

		p.fd = fd;
		p.events = POLLIN;
		p.revents = 0;
		rc = poll(&p, 1, 1000);
		now = now_cnt();
		if (rc == 0) {
			timeouts++;
			continue;
		}
		if ((rc < 0) || ((p.revents & POLLIN) == 0)) {
			errs++;
			continue;
		}
		if (read(fd, &ev, sizeof(ev)) != (ssize_t)sizeof(ev)) {
			errs++;
			continue;
		}
		lat[got++] = (now > ev.t_vblank) ? now - ev.t_vblank : 0;
	}
	(void)get_stats(&s1);
	(void)close(fd);
	d = dist(lat, (size_t)got);
	msgs = s1.pollstatus_msgs - s0.pollstatus_msgs;
	blk = s1.pollstatus_with_block - s0.pollstatus_with_block;
	printf("IPCPROBE poll events=%d errs=%d timeouts=%d wake_p50_us=%.1f wake_p90_us=%.1f wake_p99_us=%.1f wake_max_us=%.1f "
		"pollstatus_msgs=%llu per_event=%.2f block_ms_seen=%llu\n",
		got, errs, timeouts, d.p50, d.p90, d.p99, d.max, (unsigned long long)msgs,
		(got != 0) ? (double)msgs / (double)got : 0.0, (unsigned long long)blk);
	fflush(stdout);
	return (errs == 0) ? 0 : 1;
}


/* ---- fence (read-only page, coherence at rate) */

static int test_fence(void)
{
	volatile const fence_page_t *f;
	addr_t pa = 0;
	uint64_t seq, t, last = 0, first = 0, t_end, t_start, reads = 0, nonmono = 0, retries = 0;
	uint64_t *vis;
	size_t nvis = 0, cap = 4096;
	dist_t d;
	double secs, rate, expect;
	uint64_t tick_us;
	stats_t s0, s1;

	f = fence_map(0, &pa);
	vis = malloc(sizeof(uint64_t) * cap);
	if ((f == NULL) || (vis == NULL)) {
		printf("IPCPROBE fence FAIL map errno=%d\n", errno);
		free(vis);
		return 1;
	}
	tick_us = f->tick_us;
	(void)get_stats(&s0);
	t_start = now_cnt();
	t_end = t_start + us_to_cnt(2000000);
	while (now_cnt() < t_end) {
		retries += fence_read(f, &seq, &t);
		reads++;
		if (first == 0) {
			first = seq;
		}
		if (seq < last) {
			nonmono++;
		}
		if (seq > last) {
			uint64_t now = now_cnt();
			if ((last != 0) && (nvis < cap) && (now > t)) {
				vis[nvis++] = now - t; /* publish -> first observation */
			}
			last = seq;
		}
	}
	secs = (double)cnt_to_ns(now_cnt() - t_start) / 1e9;
	(void)get_stats(&s1);
	rate = (double)(last - first) / secs;
	expect = (tick_us != 0) ? 1e6 / (double)tick_us : 0.0;
	d = dist(vis, nvis);
	printf("IPCPROBE fence pa=0x%llx reads=%llu seqlock_retries=%llu nonmono=%llu seq_rate_hz=%.0f expect_hz=%.0f "
		"ticks_skipped=%llu vis_n=%zu vis_p50_us=%.2f vis_p99_us=%.2f vis_max_us=%.2f\n",
		(unsigned long long)pa, (unsigned long long)reads, (unsigned long long)retries, (unsigned long long)nonmono,
		rate, expect, (unsigned long long)(s1.ticks_skipped - s0.ticks_skipped), nvis, d.p50, d.p99, d.max);
	fflush(stdout);
	(void)munmap((void *)f, PAGE_SZ);
	free(vis);
	return (nonmono == 0) ? 0 : 1;
}


/* ---- fence-rw: MAP_PHYSMEM has no owner check, so a client can WRITE it */

static int test_fence_rw(void)
{
	volatile fence_page_t *f = (volatile fence_page_t *)fence_map(1, NULL);
	const uint64_t magic = 0xc1c1e5e5deadbeefull;
	resp_t r;
	int rc;

	if (f == NULL) {
		printf("IPCPROBE fence_rw map_rw=0 errno=%d (a refusal here would be the E1-style protection)\n", errno);
		return 0;
	}
	f->client_scratch = magic;
	__asm__ volatile("dmb ish" ::: "memory");
	rc = call(OP_FENCE_PEEK, 0, 0, 0, NULL, 0, NULL, 0, &r);
	printf("IPCPROBE fence_rw map_rw=1 client_write_visible_to_server=%d rc=%d\n", (rc == 0 && r.val == magic) ? 1 : 0, rc);
	fflush(stdout);
	f->client_scratch = 0;
	(void)munmap((void *)f, PAGE_SZ);
	return 0;
}


/* ---- fence-ro: a store through the PROT_READ view must fault (child only) */

static int test_fence_ro(void)
{
	pid_t pid;
	int st = 0;

	fflush(stdout);
	pid = fork();
	if (pid < 0) {
		printf("IPCPROBE fence_ro FAIL fork errno=%d\n", errno);
		return 1;
	}
	if (pid == 0) {
		volatile fence_page_t *f;
		if (client_resolve() < 0) {
			_exit(3);
		}
		f = (volatile fence_page_t *)fence_map(0, NULL);
		if (f == NULL) {
			_exit(4);
		}
		f->client_scratch = 1; /* expected: EL0 permission fault, child dies */
		_exit(0);
	}
	(void)waitpid(pid, &st, 0);
	{
		/* exit 3/4 = the child never got as far as the store: inconclusive */
		int ex = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
		int inconclusive = ((ex == 3) || (ex == 4)) ? 1 : 0;
		int blocked = (WIFSIGNALED(st) || (WIFEXITED(st) && (ex != 0) && (inconclusive == 0))) ? 1 : 0;
		printf("IPCPROBE fence_ro child_exited=%d exit=%d signaled=%d sig=%d write_blocked=%d inconclusive=%d\n",
			WIFEXITED(st) ? 1 : 0, ex, WIFSIGNALED(st) ? 1 : 0, WIFSIGNALED(st) ? WTERMSIG(st) : 0,
			(inconclusive != 0) ? 0 : blocked, inconclusive);
	}
	fflush(stdout);
	return 0;
}


/* ---- signal: is a thread parked in a deferred msgSend interruptible? */

static volatile uint64_t sig_at;

static void on_alarm(int sig)
{
	(void)sig;
	sig_at = now_cnt();
}


static int test_signal(void)
{
	struct sigaction sa;
	uint64_t t0, t1;
	resp_t r;
	int rc;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_alarm;
	(void)sigaction(SIGALRM, &sa, NULL);
	sig_at = 0;
	t0 = now_cnt();
	(void)alarm(1);
	rc = call(OP_DELAY, 77, 3000, 0, NULL, 0, NULL, 0, &r);
	t1 = now_cnt();
	(void)alarm(0);
	printf("IPCPROBE signal rc=%d elapsed_ms=%llu handler_ran=%d handler_ms=%lld interrupted=%d\n", rc,
		(unsigned long long)(cnt_to_ns(t1 - t0) / 1000000u), (sig_at != 0) ? 1 : 0,
		(sig_at != 0) ? (long long)(cnt_to_ns(sig_at - t0) / 1000000u) : -1LL,
		(cnt_to_ns(t1 - t0) / 1000000u < 2500u) ? 1 : 0);
	fflush(stdout);
	return 0;
}


/* ---- ridreuse: rids are lowest-free ints, reused at once */

static int test_ridreuse(void)
{
	resp_t a, b, bad;
	stats_t s;
	uint64_t t0, el;
	int rca, rcb;

	rca = call(OP_STALE, 111, 0, 0, NULL, 0, NULL, 0, &a);
	t0 = now_cnt();
	rcb = call(OP_HOLD, 222, 500, 0, NULL, 0, NULL, 0, &b);
	el = cnt_to_ns(now_cnt() - t0) / 1000000u;
	(void)usleep(600000); /* let the real (late) HOLD response be attempted */
	(void)get_stats(&s);
	(void)call(OP_BADRID, 0, 0, 0, NULL, 0, NULL, 0, &bad);
	printf("IPCPROBE ridreuse rid_stale=%d rid_hold=%d rc_stale=%d rc_hold=%d hold_got_token=%u hold_elapsed_ms=%llu "
		"victim_got_stale=%d dup_respond_rc=%lld real_respond_rc=%lld badrid_rc=%lld\n",
		a.rid, b.rid, rca, rcb, b.token, (unsigned long long)el, ((b.token == 111) && (b.flags == 0x57a1e)) ? 1 : 0,
		(long long)s.dup_respond_rc, (long long)s.late_respond_rc, (long long)(int64_t)bad.val);
	fflush(stdout);
	return 0;
}


static int client_main(int argc, char **argv)
{
	const char *t = (argc > 2) ? argv[2] : "all";
	int all = (strcmp(t, "all") == 0), fails = 0;

	if (client_resolve() < 0) {
		return 1;
	}
	if (strcmp(t, "quit") == 0) {
		resp_t r;
		int rc = call(OP_QUIT, 0, 0, 0, NULL, 0, NULL, 0, &r);
		printf("IPCPROBE quit rc=%d\n", rc);
		/* wake the server's receive loop so it notices srv.quit */
		return 0;
	}

	printf("IPCPROBE client start test=%s port=%u cntfrq=%llu\n", t, dev_oid.port, (unsigned long long)cnt_freq);
	fflush(stdout);
	if (all || (strcmp(t, "rtt") == 0)) {
		fails += test_rtt();
	}
	if (all || (strcmp(t, "deferred") == 0)) {
		fails += test_deferred();
	}
	if (all || (strcmp(t, "timeout") == 0)) {
		fails += test_timeout();
	}
	if (all || (strcmp(t, "wait") == 0)) {
		fails += test_wait();
	}
	if (all || (strcmp(t, "read") == 0)) {
		fails += test_read();
	}
	if (all || (strcmp(t, "poll") == 0)) {
		fails += test_poll();
	}
	if (all || (strcmp(t, "fence") == 0)) {
		fails += test_fence();
	}
	if (all || (strcmp(t, "fence-rw") == 0)) {
		fails += test_fence_rw();
	}
	if (all || (strcmp(t, "signal") == 0)) {
		fails += test_signal();
	}
	if (all || (strcmp(t, "ridreuse") == 0)) {
		fails += test_ridreuse();
	}
	if (all || (strcmp(t, "fence-ro") == 0)) {
		fails += test_fence_ro();
	}
	printf("IPCPROBE client done test=%s fails=%d\n", t, fails);
	fflush(stdout);
	return (fails == 0) ? 0 : 1;
}


int main(int argc, char **argv)
{
	time_init();

	if ((argc >= 2) && (strcmp(argv[1], "server") == 0)) {
		int rc = server_main(argc, argv);
		if (srv.quit != 0) {
			server_quit_watch();
		}
		return rc;
	}
	if ((argc >= 2) && (strcmp(argv[1], "client") == 0)) {
		return client_main(argc, argv);
	}
	fprintf(stderr, "usage: ipcprobe server [-r n] [-t tick_us] [-v vblank_us]\n"
					"       ipcprobe client {rtt|deferred|timeout|wait|read|poll|fence|fence-rw|fence-ro|signal|ridreuse|all|quit}\n");
	return 2;
}
