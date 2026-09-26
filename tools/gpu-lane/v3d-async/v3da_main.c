/*
 * Phoenix-RTOS
 *
 * Raspberry Pi 4 (BCM2711) V3D 4.2 asynchronous render server (/dev/v3d-async)
 *
 * NEW GPU LANE, M1 (docs/gpu-new-lane/M1-async-render-server.md). The sole
 * owner of the V3D while it runs: power/clock through /dev/vcmbox, the one MMU page
 * table, the GPU-VA space, BOs, queues, fences. It must never run next to an
 * old-lane GPU user (any game, the glamor X server, the rpi4-v3d daemon): the V3D
 * has one page-table base register and no arbitration, and this server resets it.
 *
 * Usage: rpi4-v3d-async [-f] [-i] [-I irq] [-r threads] [-p poll_us] [-m serial|pipeline]
 *                       [-k knobs] [-c chunk_kib] [-w wedge_ms] [-s stat_ms] [-v] [&]
 *   (detaches itself: psh has no job control, so `cmd &` would run in the
 *    foreground; a stray "&" argument is accepted and ignored)
 *   -f          stay in the foreground (no fork)
 *   -i          start with interrupt-driven completion (default: poll mode; switch
 *               at runtime with DBG_IRQ_MODE, e.g. `v3dasync-ping irq-on`)
 *   -I irq      interrupt number (default 106 = GIC SPI 74)
 *   -r threads  dispatch threads receiving on the port (1..4, default 2)
 *   -p poll_us  poll period while hardware is busy in poll mode (default 200)
 *   -m mode     serial (default: one hardware job at a time, the old lane's order)
 *               or pipeline (bin N+1 overlaps render N); switch at runtime with
 *               DBG_SET_MODE (`v3dasync-ping mode-pipeline`)
 *   -k knobs    cache-maintenance drops for A/B (V3DA_KNOB_*, default 0 = the
 *               old lane's full sequence)
 *   -c KiB      binner-overflow chunk size (default 1024; pool 32 MiB)
 *   -w ms       watchdog: no control-list progress this long = wedge (default 500)
 *   -s ms       periodic "V3DA srv qstat" line while GPU jobs run (default 5000, 0 = off)
 *
 * Serves: HELLO, GET_INFO, GET_PARAM, BO create/close/mmap/offset/wait (incl.
 * scanout BOs), SUBMIT_CL/TFU/CSD + the NOP test job, FENCE_WAIT, syncobjs (incl.
 * import), SCANOUT_INFO/FLIP (firmware pan), and the debug ops.
 *
 * Copyright 2026 Phoenix Systems
 * Author: Witold Bołt
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <posix/utils.h>
#include <sys/ioctl.h>
#include <sys/msg.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/threads.h>
#include <sys/types.h>

#include "v3da.h"
#include "v3da_regs.h"


v3da_srv_t srv;

#define V3DA_MAX_DISPATCH 4

static struct {
	uint8_t event_stack[16384] __attribute__((aligned(16)));
	uint8_t dispatch_stack[V3DA_MAX_DISPATCH][16384] __attribute__((aligned(16)));
	unsigned pid_mismatch;
} m;


/* ========================================================================= */
/* Clients                                                                    */
/* ========================================================================= */

static v3da_client_t *client_get(id_t id)
{
	if ((id < 1u) || (id > V3DA_MAX_CLIENTS)) {
		return NULL;
	}
	return (srv.clients[id - 1u].used != 0) ? &srv.clients[id - 1u] : NULL;
}


/* mtOpen: a new client. The positive return becomes the descriptor's oid.id. */
static int client_open(int pid)
{
	uint32_t i;
	v3da_client_t *c;

	for (i = 0u; i < V3DA_MAX_CLIENTS; i++) {
		if ((srv.clients[i].used == 0) && (v3da_slot_busy(i) == 0)) {
			c = &srv.clients[i];
			memset(c, 0, sizeof(*c));
			c->used = 1;
			c->id = i + 1u;
			c->pid = pid;
			c->slot = i;
			v3da_slot_assign(i);
			srv.nclients++;
			return (int)c->id;
		}
	}
	return -ENFILE;
}


/* mtClose (explicit close or the kernel's close at process exit): answer the
 * client's parked requests (-EPIPE; a sibling thread may still be parked), drop
 * its queued jobs and BO references. */
static void client_close(id_t id, v3da_wait_t **answer)
{
	v3da_client_t *c = client_get(id);

	if (c == NULL) {
		return;
	}
	v3da_waits_client_gone(c->id, answer);
	v3da_jobs_client_gone(c->id);
	v3da_bo_client_gone(c->id);
	c->used = 0;
	srv.nclients--;
	if (srv.verbose != 0) {
		printf("V3DA srv client %u closed\n", (unsigned)id);
	}
}


/* HELLO arrives as ioctl(fd, V3DA_IOC_HELLO) so the server sees the per-open id. */
static int client_hello(id_t id, int pid, v3da_hello_t *h)
{
	v3da_client_t *c = client_get(id);

	if (c == NULL) {
		return -EBADF;
	}
	if (h->proto != V3DA_PROTO_VERSION) {
		return -EPROTO;
	}
	if ((pid != c->pid) && (m.pid_mismatch++ == 0u)) {
		printf("V3DA srv note: HELLO pid %d != mtOpen pid %d (logged once, not enforced in part 1)\n",
			pid, c->pid);
	}
	c->hello = 1;
	memset(h, 0, sizeof(*h));
	h->proto = V3DA_PROTO_VERSION;
	h->client_id = c->id;
	h->slot = c->slot;
	h->slot_gen = (uint32_t)srv.slot_gen[c->slot];
	h->server_pid = (uint32_t)getpid();
	h->flags = srv.fp->hdr.flags;
	h->fence_page.kind = V3DA_MEM_PHYS;
	h->fence_page.cache = V3DA_CACHE_CACHED;
	h->fence_page.size = V3DA_FENCE_PAGE_SIZE;
	h->fence_page.addr = (uint64_t)srv.fp_pa;
	return 0;
}


/* ========================================================================= */
/* Requests                                                                   */
/* ========================================================================= */

static void stats(v3da_stats_t *s)
{
	memset(s, 0, sizeof(*s));
	s->clients = srv.nclients;
	v3da_bo_counts(&s->bos_live, &s->bos_quarantined, &s->bos_pooled);
	s->bo_quarantine_passed = srv.bo_quarantine_passed;
	s->pages_to_kernel = srv.pages_to_kernel;
	s->parked = srv.nparked;
	s->parked_max = srv.parked_max;
	s->irq_count = srv.hw.irq_count;
	s->tlb_flushes = srv.hw.tlb_flushes;
	s->loops = srv.loops;
	s->timeouts = srv.timeouts;
	s->nops_done = srv.nops_done;
	s->resets = srv.resets;
}


static int irq_mode(uint32_t on, v3da_irq_mode_resp_t *out)
{
	int rc = 0, q;

	/* Switching masks and clearing latched status mid-job would lose a completion. */
	for (q = 0; q < V3DA_Q_COUNT; q++) {
		if ((q != V3DA_Q_CPU) && (srv.q[q].active != NULL)) {
			out->rc = -EBUSY;
			out->mode = (uint32_t)srv.hw.irq_on;
			out->irq = srv.hw.irq_num;
			return -EBUSY;
		}
	}
	if (on != 0u) {
		rc = v3da_hw_irq_enable(&srv.hw);
		if (rc >= 0) {
			srv.fp->hdr.flags = (srv.fp->hdr.flags | V3DA_FP_IRQ) & ~V3DA_FP_STORM;
		}
	}
	else {
		v3da_hw_irq_disable(&srv.hw);
		srv.fp->hdr.flags &= ~V3DA_FP_IRQ;
	}
	out->rc = (rc < 0) ? rc : 0;
	out->mode = (uint32_t)srv.hw.irq_on;
	out->irq = srv.hw.irq_num;
	printf("V3DA srv irq mode=%s irq=%u rc=%d\n", (srv.hw.irq_on != 0) ? "irq" : "poll", srv.hw.irq_num, rc);
	return (rc < 0) ? rc : 0;
}


/* A raw request. Returns 1 to respond now, 0 when the request was parked. Called
 * with srv.lock held; *answer collects parked requests claimed on the way. */
static int handle_raw(msg_t *msg, msg_rid_t rid, v3da_wait_t **answer)
{
	const v3da_req_t *rq = (const v3da_req_t *)msg->i.raw;
	v3da_req_t req;
	v3da_resp_t *r = (v3da_resp_t *)msg->o.raw;
	v3da_client_t *c;
	v3da_bo_t *b;
	v3da_fence_t f[V3DA_Q_COUNT];
	uint32_t i, n;
	int rc = -ENOSYS;

	memcpy(&req, rq, sizeof(req));   /* o.raw and i.raw are distinct, but keep it simple */
	memset(r, 0, sizeof(*r));
	r->op = req.op;
	msg->o.err = EOK;

	c = client_get(msg->oid.id);
	if (c == NULL) {
		r->err = -EBADF;
		return 1;
	}
	if ((msg->pid != c->pid) && (m.pid_mismatch++ == 0u)) {
		printf("V3DA srv note: request pid %d != client pid %d (logged once, not enforced in part 1)\n",
			msg->pid, c->pid);
	}

	switch (req.op) {
		case V3DA_OP_GET_INFO:
			memcpy(r->u.info.ident, srv.hw.ident, sizeof(r->u.info.ident));
			r->u.info.irq_mode = (uint32_t)srv.hw.irq_on;
			r->u.info.irq_num = srv.hw.irq_num;
			r->u.info.clk_rate_hz = srv.hw.clk_rate_hz;
			r->u.info.clk_meas_hz = srv.hw.clk_meas_hz;
			r->u.info.max_bos = V3DA_MAX_BOS;
			r->u.info.nclients = srv.nclients;
			rc = 0;
			break;

		case V3DA_OP_GET_PARAM:
			rc = v3da_get_param(req.u.get_param.param, &r->u.get_param.value);
			break;

		case V3DA_OP_BO_CREATE:
			rc = v3da_bo_create(c->id, req.u.bo_create.size, req.u.bo_create.flags, &r->u.bo_create);
			break;

		case V3DA_OP_BO_CLOSE:
			rc = v3da_bo_close(c->id, req.u.bo.handle);
			break;

		case V3DA_OP_BO_MMAP:
			rc = v3da_bo_mmap(req.u.bo.handle, &r->u.bo);
			break;

		case V3DA_OP_BO_GET_OFFSET:
			rc = v3da_bo_offset(req.u.bo.handle, &r->u.bo.gpuva);
			break;

		case V3DA_OP_BO_WAIT:
			b = v3da_bo_find(req.u.bo.handle);
			if (b == NULL) {
				rc = -EINVAL;
				break;
			}
			memcpy(f, b->last, sizeof(f));
			rc = v3da_wait_park(c, V3DA_WAIT_BO, msg, rid, f, NULL, V3DA_Q_COUNT, 1, 0, req.u.bo.timeout_ms);
			if (rc == 0) {
				return 0;
			}
			rc = (rc > 0) ? 0 : rc;
			break;

		case V3DA_OP_SUBMIT_NOP:
			rc = v3da_submit_nop(c, req.u.nop.delay_us, &r->u.fence.fence);
			break;

		case V3DA_OP_SUBMIT_CL:
		case V3DA_OP_SUBMIT_TFU:
		case V3DA_OP_SUBMIT_CSD:
			/* i.data is mapped only until we respond: v3da_submit copies what it keeps. */
			rc = v3da_submit(c, (int)req.op, &req.u.submit, msg->i.data, msg->i.size, &r->u.submit);
			break;

		case V3DA_OP_SYNCOBJ_IMPORT:
			rc = v3da_syncobj_import(c, req.u.syncobj_import.handle, &req.u.syncobj_import.fence);
			break;

		case V3DA_OP_SCANOUT_INFO:
			rc = v3da_scanout_info(&req.u.scanout, &r->u.scanout);
			break;

		case V3DA_OP_FLIP:
			rc = v3da_flip(c, &req.u.flip, &r->u.flip);
			break;

		case V3DA_OP_DBG_SET_MODE:
			rc = v3da_set_mode(&req.u.mode, &r->u.mode);
			break;

		case V3DA_OP_DBG_QSTATS:
			rc = v3da_qstats(&req.u.qstats, r);
			break;

		case V3DA_OP_FENCE_WAIT:
			if (v3da_fence_valid(c, &req.u.fence_wait.fence) == 0) {
				rc = -EINVAL;
				break;
			}
			rc = v3da_wait_park(c, V3DA_WAIT_FENCE, msg, rid, &req.u.fence_wait.fence, NULL, 1u, 1, 0,
				req.u.fence_wait.timeout_ms);
			if (rc == 0) {
				return 0;
			}
			rc = (rc > 0) ? 0 : rc;
			break;

		case V3DA_OP_SYNCOBJ_CREATE:
			rc = v3da_syncobj_create(c, req.u.syncobj.flags, &r->u.syncobj.handle);
			break;

		case V3DA_OP_SYNCOBJ_DESTROY:
			rc = v3da_syncobj_destroy(c, req.u.syncobj.handle);
			break;

		case V3DA_OP_SYNCOBJ_RESET:
			rc = v3da_syncobj_reset(c, req.u.syncobj.handle);
			break;

		case V3DA_OP_SYNCOBJ_SIGNAL:
			rc = v3da_syncobj_signal(c, req.u.syncobj.handle);
			break;

		case V3DA_OP_SYNCOBJ_QUERY:
			rc = v3da_syncobj_query(c, req.u.syncobj.handle, &r->u.syncobj);
			break;

		case V3DA_OP_SYNCOBJ_WAIT:
			n = req.u.syncobj.count;
			if ((n == 0u) || (n > V3DA_SYNCOBJ_WAIT_MAX)) {
				rc = -EINVAL;
				break;
			}
			for (i = 0u; i < n; i++) {
				if (v3da_syncobj_get(c, req.u.syncobj.handles[i]) == NULL) {
					break;
				}
			}
			if (i != n) {
				rc = -EINVAL;
				break;
			}
			rc = v3da_wait_park(c, V3DA_WAIT_SYNCOBJ, msg, rid, NULL, req.u.syncobj.handles, n,
				((req.u.syncobj.flags & V3DA_SYNCOBJ_WAIT_ALL) != 0u) ? 1 : 0,
				((req.u.syncobj.flags & V3DA_SYNCOBJ_WAIT_FOR_SUBMIT) != 0u) ? 1 : 0,
				req.u.syncobj.timeout_ms);
			if (rc == 0) {
				return 0;
			}
			rc = (rc > 0) ? 0 : rc;
			break;

		case V3DA_OP_DBG_IRQ_MODE:
			rc = irq_mode(req.u.irq_mode.on, &r->u.irq_mode);
			v3da_kick_event_thread();
			break;

		case V3DA_OP_DBG_BO_CHECKSUM:
			rc = v3da_bo_checksum(req.u.bo_checksum.handle, req.u.bo_checksum.offset, req.u.bo_checksum.len,
				&r->u.bo_checksum);
			break;

		case V3DA_OP_DBG_STATS:
			stats(&r->u.stats);
			rc = 0;
			break;

		case V3DA_OP_DBG_QUIT:
			r->u.quit.inflight = v3da_jobs_inflight();
			if (r->u.quit.inflight != 0u) {
				rc = -EBUSY;   /* never exit with a job in flight (design 6.4) */
				break;
			}
			r->u.quit.parked = srv.nparked;
			v3da_waits_all(answer, -ENODEV);   /* the server is going away */
			v3da_hw_shutdown(&srv.hw);
			srv.fp->hdr.flags |= V3DA_FP_EXITED;
			srv.quit = 1;
			(void)condSignal(srv.cond);
			rc = 0;
			break;

		/* later milestones */
		case V3DA_OP_SUBMIT_CPU:
		case V3DA_OP_BO_IMPORT:
		case V3DA_OP_PERFMON_CREATE:
		case V3DA_OP_PERFMON_DESTROY:
		case V3DA_OP_PERFMON_GET_VALUES:
		case V3DA_OP_PERFMON_GET_COUNTER:
		case V3DA_OP_SCANOUT_BO:
			rc = -ENOSYS;
			break;

		default:
			rc = -EINVAL;
			break;
	}
	r->err = rc;
	return 1;
}


/* An ioctl()-packed mtDevCtl: only HELLO. */
static void handle_ioctl(msg_t *msg)
{
	unsigned long request;
	id_t id;
	const void *in;
	v3da_hello_t h;
	int rc;

	in = ioctl_unpack(msg, &request, &id);
	if ((request != V3DA_IOC_HELLO) || (in == NULL)) {
		ioctl_setResponse(msg, request, -ENOTTY, NULL);
		return;
	}
	memcpy(&h, in, sizeof(h));
	(void)mutexLock(srv.lock);
	rc = client_hello(id, msg->pid, &h);
	(void)mutexUnlock(srv.lock);
	ioctl_setResponse(msg, request, rc, (rc == 0) ? &h : NULL);
}


static void dispatch_loop(void *arg)
{
	msg_t msg;
	msg_rid_t rid;
	v3da_wait_t *answer;
	v3da_irq_selftest_resp_t st;
	v3da_resp_t *r;
	int err, respond, id, quitting;

	(void)arg;

	for (;;) {
		err = msgRecv(srv.port, &msg, &rid);
		if (err < 0) {
			if (err == -EINTR) {
				continue;
			}
			break;
		}

		respond = 1;
		quitting = 0;
		answer = NULL;
		switch (msg.type) {
			case mtOpen:
				(void)mutexLock(srv.lock);
				id = client_open(msg.pid);
				(void)mutexUnlock(srv.lock);
				msg.o.err = id;   /* > 0: becomes the descriptor's oid.id */
				if ((srv.verbose != 0) && (id > 0)) {
					printf("V3DA srv client %d opened by pid %d\n", id, msg.pid);
				}
				break;

			case mtClose:
				(void)mutexLock(srv.lock);
				client_close(msg.oid.id, &answer);
				(void)mutexUnlock(srv.lock);
				msg.o.err = EOK;
				break;

			case mtDevCtl:
				if (((const uint32_t *)msg.i.raw)[0] != V3DA_MAGIC) {
					handle_ioctl(&msg);
					break;
				}
				if (((const v3da_req_t *)msg.i.raw)->op == V3DA_OP_DBG_IRQ_SELFTEST) {
					/* Sleeps between raising and collecting: runs unlocked. */
					r = (v3da_resp_t *)msg.o.raw;
					memset(r, 0, sizeof(*r));
					r->op = V3DA_OP_DBG_IRQ_SELFTEST;
					r->err = v3da_hw_selftest(&srv.hw, &st);
					r->u.irq_selftest = st;
					msg.o.err = EOK;
					break;
				}
				(void)mutexLock(srv.lock);
				respond = handle_raw(&msg, rid, &answer);
				quitting = srv.quit;
				(void)mutexUnlock(srv.lock);
				break;

			case mtGetAttr:
				/* Path resolution asks every component for atMode (the symlink check in
				 * libphoenix _readlink_abs); answer like rpi4-fb does. */
				if (msg.i.attr.type == atMode) {
					msg.o.attr.val = S_IFCHR | 0666;
					msg.o.err = EOK;
				}
				else {
					msg.o.err = -EINVAL;
				}
				break;

			case mtRead:
			case mtWrite:
				msg.o.err = -EINVAL;
				break;

			default:
				msg.o.err = -ENOSYS;
				break;
		}

		/* Parked requests claimed on the way (client death, quit) first: each was
		 * removed from srv.waits under the lock, so this is their only answer. */
		v3da_waits_answer(answer);
		if (respond != 0) {
			(void)msgRespond(srv.port, &msg, rid);
		}
		if (quitting != 0) {
			printf("V3DA srv exit parked=%u inflight=%u\n",
				((v3da_resp_t *)msg.o.raw)->u.quit.parked, ((v3da_resp_t *)msg.o.raw)->u.quit.inflight);
			usleep(50000);   /* let in-flight responds of other threads finish */
			exit(0);
		}
	}
}


static void usage(const char *prog)
{
	printf("usage: %s [-f] [-i] [-I irq] [-r threads] [-p poll_us] [-m serial|pipeline] [-k knobs] [-c chunk_kib] "
		"[-w wedge_ms] [-s stat_ms] [-v]\n", prog);
}


int main(int argc, char **argv)
{
	oid_t dev;
	int c, rc, i, nthreads = 2, irq_at_start = 0, foreground = 0, readyfd = -1;

	setvbuf(stdout, NULL, _IOLBF, 0);   /* every graded line is a stdout line */
	srv.hw.irq_num = V3D_IRQ;
	srv.poll_us = 200u;
	srv.mode = V3DA_MODE_SERIAL;
	srv.knobs = 0u;
	srv.ovf_chunk_kib = 1024u;
	srv.wedge_ms = 500u;
	srv.stat_ms = 5000u;

	while ((c = getopt(argc, argv, "fiI:r:p:m:k:c:w:s:vh")) != -1) {
		switch (c) {
			case 'm':
				if (strcmp(optarg, "pipeline") == 0) {
					srv.mode = V3DA_MODE_PIPELINE;
				}
				else if (strcmp(optarg, "serial") == 0) {
					srv.mode = V3DA_MODE_SERIAL;
				}
				else {
					usage(argv[0]);
					return 1;
				}
				break;
			case 'k': srv.knobs = (uint32_t)strtoul(optarg, NULL, 0) & V3DA_KNOB_ALL; break;
			case 'c': srv.ovf_chunk_kib = (uint32_t)strtoul(optarg, NULL, 0); break;
			case 'w': srv.wedge_ms = (uint32_t)strtoul(optarg, NULL, 0); break;
			case 's': srv.stat_ms = (uint32_t)strtoul(optarg, NULL, 0); break;
			case 'f': foreground = 1; break;
			case 'i': irq_at_start = 1; break;
			case 'I': srv.hw.irq_num = (unsigned)strtoul(optarg, NULL, 0); break;
			case 'r': nthreads = atoi(optarg); break;
			case 'p': srv.poll_us = (uint32_t)strtoul(optarg, NULL, 0); break;
			case 'v': srv.verbose = 1; break;
			default: usage(argv[0]); return 1;
		}
	}
	/* psh has no job control: a trailing "&" is NOT a background request, it
	 * arrives as a plain argument (and the command runs in the foreground). The
	 * server detaches itself anyway, so tolerate it. */
	for (i = optind; i < argc; i++) {
		if (strcmp(argv[i], "&") != 0) {
			printf("V3DA srv unexpected argument '%s'\n", argv[i]);
			usage(argv[0]);
			return 1;
		}
	}
	if ((nthreads < 1) || (nthreads > V3DA_MAX_DISPATCH)) {
		nthreads = 2;
	}
	if (srv.poll_us == 0u) {
		srv.poll_us = 200u;
	}
	if (srv.wedge_ms < 100u) {
		srv.wedge_ms = 100u;
	}

	/* Detach, so psh gets its prompt back (the ipcprobe pattern). This happens
	 * BEFORE any port, thread, mapping, GPU power-on or interrupt() exists: none of
	 * those survive a fork, so the child does all of the setup. The parent waits on
	 * a pipe for one byte 'R', written by the child once /dev/v3d-async is
	 * registered AND the GPU is owned and the threads are running; a child that
	 * fails exits, which closes the pipe (EOF). */
	if (foreground == 0) {
		int pfd[2];
		pid_t pid;
		char r = 0;

		if (pipe(pfd) < 0) {
			printf("V3DA srv FAIL pipe errno=%d\n", errno);
			return 1;
		}
		fflush(stdout);
		pid = fork();
		if (pid < 0) {
			printf("V3DA srv FAIL fork errno=%d\n", errno);
			return 1;
		}
		if (pid > 0) {
			close(pfd[1]);
			if ((read(pfd[0], &r, 1) != 1) || (r != 'R')) {
				printf("V3DA srv FAIL child did not come up (see its lines above)\n");
				return 1;
			}
			printf("V3DA srv detached pid=%d\n", (int)pid);
			fflush(stdout);
			_exit(0);
		}
		close(pfd[0]);
		readyfd = pfd[1];
	}

	/* Claim the device node FIRST: create_dev is the single-owner guard. A second
	 * instance must fail before it touches power or MMU_PT_PA_BASE. */
	if (portCreate(&srv.port) != EOK) {
		printf("V3DA srv portCreate failed\n");
		return 1;
	}
	dev.port = srv.port;
	dev.id = 0;
	if (create_dev(&dev, V3DA_DEV_NAME) < 0) {
		printf("V3DA srv could not create /dev/%s (already owned?)\n", V3DA_DEV_NAME);
		return 2;
	}
	if ((mutexCreate(&srv.lock) != EOK) || (condCreate(&srv.cond) != EOK)) {
		printf("V3DA srv lock/cond create failed\n");
		return 1;
	}
	srv.hw.irq_cond = srv.cond;

	rc = v3da_hw_init(&srv.hw);
	if (rc != 0) {
		printf("V3DA srv GPU init failed (%d); refusing to serve\n", rc);
		return 3;
	}
	rc = v3da_sched_init();
	if (rc != 0) {
		printf("V3DA srv fence page / overflow pool allocation failed (%d)\n", rc);
		return 4;
	}

	if (irq_at_start != 0) {
		v3da_irq_mode_resp_t im;
		(void)mutexLock(srv.lock);
		(void)irq_mode(1u, &im);
		(void)mutexUnlock(srv.lock);
	}

	if (beginthread(v3da_event_thread, 2, m.event_stack, sizeof(m.event_stack), NULL) != 0) {
		printf("V3DA srv event thread start failed\n");
		return 5;
	}
	for (i = 1; i < nthreads; i++) {
		if (beginthread(dispatch_loop, 3, m.dispatch_stack[i], sizeof(m.dispatch_stack[i]), NULL) != 0) {
			printf("V3DA srv dispatch thread %d start failed\n", i);
		}
	}

	printf("V3DA srv ready dev=/dev/%s irq=%s irqnum=%u threads=%d poll_us=%u fence_pa=0x%08lx slots=%u "
		"mode=%s knobs=0x%02x ovf=%ux%uKiB wedge_ms=%u proto=%u\n",
		V3DA_DEV_NAME, (srv.hw.irq_on != 0) ? "on" : "off", srv.hw.irq_num, nthreads, srv.poll_us,
		(unsigned long)srv.fp_pa, V3DA_FENCE_NSLOTS, (srv.mode == V3DA_MODE_SERIAL) ? "serial" : "pipeline",
		srv.knobs, srv.ovf.nchunks, srv.ovf.chunk_bytes / 1024u, srv.wedge_ms, V3DA_PROTO_VERSION);

	if (readyfd >= 0) {
		char r = 'R';

		(void)write(readyfd, &r, 1);
		close(readyfd);
	}

	(void)setPriority(3);
	dispatch_loop(NULL);
	return 0;
}
