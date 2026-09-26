/*
 * Phoenix-RTOS
 *
 * Raspberry Pi 4 (BCM2711) V3D 4.2 asynchronous render server - client library
 *
 * Connect: open("/dev/v3d-async") (the server's mtOpen answer becomes this
 * descriptor's oid.id = our client id; the kernel's mtClose for it at exit is the
 * server's death notice), then HELLO as an ioctl() on that descriptor, then map
 * the fence page. Every later request is a direct msgSend to {port, client id}.
 *
 * Waits: the fence page answers "already signalled" with one load; otherwise a
 * short spin, then bounded server waits - never longer than V3DA_WAIT_MAX_MS per
 * parked request, because a parked Phoenix client can be neither interrupted nor
 * killed (E5). V3DA_WAIT=poll never parks at all (spins on the fence page with
 * backoff and gives up if the server's heartbeat stops).
 *
 * Copyright 2026 Phoenix Systems
 * Author: Witold Bołt
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/msg.h>
#include <sys/threads.h>

#include "libv3da-client.h"


#define V3DA_SPIN_US       50u      /* fast-path spin before the first server wait */
#define V3DA_DEAD_HB_US    3000000u /* poll mode: heartbeat frozen this long = dead server */


static uint64_t now_us(void)
{
	struct timespec ts;

	(void)clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}


void *v3da_map(const v3da_memref_t *m, int writable)
{
	void *p;
	int flags;

	if ((m->kind != V3DA_MEM_PHYS) || (m->size == 0u)) {
		return NULL;   /* V3DA_MEM_OID: open("/gpubuf/<id>") + mmap(fd) once E1 lands */
	}
	flags = MAP_PHYSMEM | MAP_ANONYMOUS;
	if (m->cache == V3DA_CACHE_UNCACHED) {
		flags |= MAP_UNCACHED;   /* must match the server's mapping: one memory type per page */
	}
	p = mmap(NULL, (size_t)m->size, PROT_READ | ((writable != 0) ? PROT_WRITE : 0), flags, -1, (addr_t)m->addr);
	return (p == MAP_FAILED) ? NULL : p;
}


void v3da_unmap(void *p, const v3da_memref_t *m)
{
	if (p != NULL) {
		(void)munmap(p, (size_t)m->size);
	}
}


int v3da_connect(v3da_conn_t *c)
{
	oid_t dev;
	int tries, rc;
	const char *e;
	void *fp;

	memset(c, 0, sizeof(*c));
	c->fd = -1;
	e = getenv("V3DA_WAIT");
	c->wait_poll = ((e != NULL) && (strcmp(e, "poll") == 0)) ? 1 : 0;

	for (tries = 0; tries < 50; tries++) {
		/* O_RDONLY: an O_RDWR open additionally stat()s the node (E1 section 1). */
		c->fd = open("/dev/" V3DA_DEV_NAME, O_RDONLY);
		if (c->fd >= 0) {
			break;
		}
		usleep(100 * 1000);
	}
	if (c->fd < 0) {
		return -ENOENT;
	}
	if (lookup("/dev/" V3DA_DEV_NAME, NULL, &dev) < 0) {
		v3da_disconnect(c);
		return -ENOENT;
	}

	c->hello.proto = V3DA_PROTO_VERSION;
	rc = ioctl(c->fd, V3DA_IOC_HELLO, &c->hello);
	if (rc < 0) {
		rc = -errno;
		v3da_disconnect(c);
		return (rc != 0) ? rc : -EIO;
	}
	if (c->hello.client_id == 0u) {
		v3da_disconnect(c);
		return -EPROTO;
	}
	c->oid.port = dev.port;
	c->oid.id = c->hello.client_id;
	if (mutexCreate(&c->lock) != EOK) {
		v3da_disconnect(c);
		return -ENOMEM;
	}
	c->lock_ok = 1;

	fp = v3da_map(&c->hello.fence_page, 0);
	if (fp == NULL) {
		v3da_disconnect(c);
		return -ENOMEM;
	}
	c->fp = fp;
	return 0;
}


void v3da_disconnect(v3da_conn_t *c)
{
	if (c->sbuf != NULL) {
		(void)munmap(c->sbuf, c->sbuf_size);
		c->sbuf = NULL;
		c->sbuf_size = 0u;
	}
	if (c->lock_ok != 0) {
		(void)resourceDestroy(c->lock);
		c->lock_ok = 0;
	}
	if (c->fp != NULL) {
		v3da_unmap((void *)c->fp, &c->hello.fence_page);
		c->fp = NULL;
	}
	if (c->fd >= 0) {
		(void)close(c->fd);   /* -> mtClose: the server drops our state */
		c->fd = -1;
	}
}


int v3da_call(v3da_conn_t *c, v3da_req_t *req, v3da_resp_t *resp)
{
	msg_t msg;
	int err;

	req->magic = V3DA_MAGIC;
	memset(&msg, 0, sizeof(msg));
	msg.type = mtDevCtl;
	msg.oid = c->oid;
	memcpy(msg.i.raw, req, sizeof(*req));

	err = msgSend(c->oid.port, &msg);
	if (err < 0) {
		return err;
	}
	if (msg.o.err < 0) {
		return msg.o.err;
	}
	if (resp != NULL) {
		memcpy(resp, msg.o.raw, sizeof(*resp));
	}
	return ((const v3da_resp_t *)msg.o.raw)->err;
}


static void req_init(v3da_req_t *req, uint32_t op)
{
	memset(req, 0, sizeof(*req));
	req->op = op;
}


int v3da_get_info(v3da_conn_t *c, v3da_info_t *out)
{
	v3da_req_t req;
	v3da_resp_t resp;
	int rc;

	req_init(&req, V3DA_OP_GET_INFO);
	rc = v3da_call(c, &req, &resp);
	if (rc == 0) {
		*out = resp.u.info;
	}
	return rc;
}


int v3da_get_param(v3da_conn_t *c, uint32_t param, uint64_t *value)
{
	v3da_req_t req;
	v3da_resp_t resp;
	int rc;

	req_init(&req, V3DA_OP_GET_PARAM);
	req.u.get_param.param = param;
	rc = v3da_call(c, &req, &resp);
	if (rc == 0) {
		*value = resp.u.get_param.value;
	}
	return rc;
}


int v3da_bo_create(v3da_conn_t *c, uint32_t size, uint32_t flags, v3da_bo_create_resp_t *out)
{
	v3da_req_t req;
	v3da_resp_t resp;
	int rc;

	req_init(&req, V3DA_OP_BO_CREATE);
	req.u.bo_create.size = size;
	req.u.bo_create.flags = flags;
	rc = v3da_call(c, &req, &resp);
	if (rc == 0) {
		*out = resp.u.bo_create;
	}
	return rc;
}


static int bo_simple(v3da_conn_t *c, uint32_t op, uint32_t handle, v3da_bo_resp_t *out)
{
	v3da_req_t req;
	v3da_resp_t resp;
	int rc;

	req_init(&req, op);
	req.u.bo.handle = handle;
	rc = v3da_call(c, &req, &resp);
	if ((rc == 0) && (out != NULL)) {
		*out = resp.u.bo;
	}
	return rc;
}


int v3da_bo_close(v3da_conn_t *c, uint32_t handle)
{
	return bo_simple(c, V3DA_OP_BO_CLOSE, handle, NULL);
}


int v3da_bo_mmap(v3da_conn_t *c, uint32_t handle, v3da_bo_resp_t *out)
{
	return bo_simple(c, V3DA_OP_BO_MMAP, handle, out);
}


int v3da_bo_offset(v3da_conn_t *c, uint32_t handle, uint32_t *gpuva)
{
	v3da_bo_resp_t r;
	int rc = bo_simple(c, V3DA_OP_BO_GET_OFFSET, handle, &r);

	if (rc == 0) {
		*gpuva = r.gpuva;
	}
	return rc;
}


/* Remaining budget of an absolute deadline as a per-request server timeout. */
static uint32_t slice_ms(uint64_t deadline, int forever)
{
	uint64_t now = now_us(), left;

	if (forever != 0) {
		return V3DA_WAIT_MAX_MS;
	}
	if (now >= deadline) {
		return 0u;
	}
	left = (deadline - now + 999u) / 1000u;
	return (left > V3DA_WAIT_MAX_MS) ? V3DA_WAIT_MAX_MS : (uint32_t)left;
}


int v3da_bo_wait(v3da_conn_t *c, uint32_t handle, int64_t timeout_ns)
{
	v3da_req_t req;
	uint64_t deadline = now_us() + ((timeout_ns > 0) ? (uint64_t)timeout_ns / 1000u : 0u);
	int forever = (timeout_ns < 0) ? 1 : 0, rc;

	do {
		req_init(&req, V3DA_OP_BO_WAIT);
		req.u.bo.handle = handle;
		req.u.bo.timeout_ms = slice_ms(deadline, forever);
		c->ipc_waits++;
		rc = v3da_call(c, &req, NULL);
	} while ((rc == -ETIMEDOUT) && ((forever != 0) || (now_us() < deadline)));
	return rc;
}


int v3da_submit_nop(v3da_conn_t *c, uint32_t delay_us, v3da_fence_t *out)
{
	v3da_req_t req;
	v3da_resp_t resp;
	int rc;

	req_init(&req, V3DA_OP_SUBMIT_NOP);
	req.u.nop.delay_us = delay_us;
	rc = v3da_call(c, &req, &resp);
	if (rc == 0) {
		*out = resp.u.fence.fence;
	}
	return rc;
}


/* The flat submit buffer lives in whole pages we own (mmap): a page-aligned start
 * AND end means the kernel maps it into the server with no shadow page or copy
 * (E5 section 1). */
int v3da_submit(v3da_conn_t *c, uint32_t op, const void *desc, uint32_t desc_size,
	const uint32_t *bos, uint32_t nbo, const v3da_sem_t *in, uint32_t nin,
	const v3da_sem_t *out, uint32_t nout, v3da_submit_resp_t *resp)
{
	v3da_req_t req;
	v3da_resp_t rsp;
	msg_t msg;
	size_t need, alloc;
	uint8_t *p;
	void *nb;
	int err;

	if ((nbo > V3DA_SUBMIT_MAX_BOS) || (nin > V3DA_SUBMIT_MAX_SEMS) || (nout > V3DA_SUBMIT_MAX_SEMS)) {
		return -EINVAL;
	}
	need = desc_size + (size_t)nbo * sizeof(uint32_t) + ((size_t)nin + nout) * sizeof(v3da_sem_t);
	alloc = (need + _PAGE_SIZE - 1u) & ~((size_t)_PAGE_SIZE - 1u);

	(void)mutexLock(c->lock);
	if (c->sbuf_size < alloc) {
		nb = mmap(NULL, alloc, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, -1, 0);
		if (nb == MAP_FAILED) {
			(void)mutexUnlock(c->lock);
			return -ENOMEM;
		}
		if (c->sbuf != NULL) {
			(void)munmap(c->sbuf, c->sbuf_size);
		}
		c->sbuf = nb;
		c->sbuf_size = alloc;
	}
	p = c->sbuf;
	memcpy(p, desc, desc_size);
	p += desc_size;
	if (nbo != 0u) {
		memcpy(p, bos, (size_t)nbo * sizeof(uint32_t));
		p += (size_t)nbo * sizeof(uint32_t);
	}
	if (nin != 0u) {
		memcpy(p, in, (size_t)nin * sizeof(v3da_sem_t));
		p += (size_t)nin * sizeof(v3da_sem_t);
	}
	if (nout != 0u) {
		memcpy(p, out, (size_t)nout * sizeof(v3da_sem_t));
	}

	memset(&req, 0, sizeof(req));
	req.magic = V3DA_MAGIC;
	req.op = op;
	req.u.submit.desc_size = desc_size;
	req.u.submit.nbo = nbo;
	req.u.submit.nin = nin;
	req.u.submit.nout = nout;

	memset(&msg, 0, sizeof(msg));
	msg.type = mtDevCtl;
	msg.oid = c->oid;
	memcpy(msg.i.raw, &req, sizeof(req));
	msg.i.data = c->sbuf;
	msg.i.size = alloc;
	err = msgSend(c->oid.port, &msg);
	(void)mutexUnlock(c->lock);
	if (err < 0) {
		return err;
	}
	if (msg.o.err < 0) {
		return msg.o.err;
	}
	memcpy(&rsp, msg.o.raw, sizeof(rsp));
	if ((rsp.err == 0) && (resp != NULL)) {
		*resp = rsp.u.submit;
	}
	return rsp.err;
}


int v3da_fence_error(const v3da_conn_t *c, const v3da_fence_t *f)
{
	const volatile v3da_fence_slot_t *s;
	uint64_t es;

	if ((c->fp == NULL) || (f->slot >= V3DA_FENCE_NSLOTS)) {
		return 0;
	}
	s = &c->fp->slot[f->slot];
	if ((uint32_t)__atomic_load_n(&s->gen, __ATOMIC_ACQUIRE) != f->gen) {
		return 0;
	}
	es = __atomic_load_n(&s->error_seq, __ATOMIC_ACQUIRE);
	return ((es != 0u) && ((es >> 56) == f->queue) && ((es & 0x00ffffffffffffffULL) >= f->seqno)) ? 1 : 0;
}


int v3da_fence_signaled(const v3da_conn_t *c, const v3da_fence_t *f)
{
	const volatile v3da_fence_slot_t *s;

	if ((c->fp == NULL) || (f->slot >= V3DA_FENCE_NSLOTS) || (f->queue >= V3DA_Q_COUNT)) {
		return 0;
	}
	s = &c->fp->slot[f->slot];
	if ((uint32_t)__atomic_load_n(&s->gen, __ATOMIC_ACQUIRE) != f->gen) {
		return 1;   /* slot reassigned: every fence of the old owner completed */
	}
	return (__atomic_load_n(&s->completed[f->queue], __ATOMIC_ACQUIRE) >= f->seqno) ? 1 : 0;
}


int v3da_fence_wait_once(v3da_conn_t *c, const v3da_fence_t *f, uint32_t timeout_ms, v3da_fence_wait_resp_t *out)
{
	v3da_req_t req;
	v3da_resp_t resp;
	int rc;

	req_init(&req, V3DA_OP_FENCE_WAIT);
	req.u.fence_wait.fence = *f;
	req.u.fence_wait.timeout_ms = timeout_ms;
	c->ipc_waits++;
	rc = v3da_call(c, &req, &resp);
	if ((rc == 0) && (out != NULL)) {
		*out = resp.u.fence_wait;
	}
	return rc;
}


int v3da_fence_wait(v3da_conn_t *c, const v3da_fence_t *f, int64_t timeout_ns)
{
	uint64_t t0 = now_us(), deadline, hb, hb_t, delay = 20u;
	int forever = (timeout_ns < 0) ? 1 : 0, rc;

	deadline = t0 + ((timeout_ns > 0) ? (uint64_t)timeout_ns / 1000u : 0u);

	/* fast path + short spin */
	do {
		if (v3da_fence_signaled(c, f) != 0) {
			return 0;
		}
	} while ((now_us() - t0) < V3DA_SPIN_US);
	if ((forever == 0) && (now_us() >= deadline)) {
		return -ETIMEDOUT;
	}

	if (c->wait_poll != 0) {
		/* Never park: back off on the fence page, watch the heartbeat. */
		hb = __atomic_load_n(&c->fp->hdr.heartbeat, __ATOMIC_ACQUIRE);
		hb_t = now_us();
		for (;;) {
			if (v3da_fence_signaled(c, f) != 0) {
				return 0;
			}
			if ((forever == 0) && (now_us() >= deadline)) {
				return -ETIMEDOUT;
			}
			if (__atomic_load_n(&c->fp->hdr.heartbeat, __ATOMIC_ACQUIRE) != hb) {
				hb = __atomic_load_n(&c->fp->hdr.heartbeat, __ATOMIC_ACQUIRE);
				hb_t = now_us();
			}
			else if ((now_us() - hb_t) > V3DA_DEAD_HB_US) {
				return -EPIPE;   /* the server stopped looping */
			}
			usleep((useconds_t)delay);
			if (delay < 2000u) {
				delay *= 2u;
			}
		}
	}

	do {
		rc = v3da_fence_wait_once(c, f, slice_ms(deadline, forever), NULL);
	} while ((rc == -ETIMEDOUT) && ((forever != 0) || (now_us() < deadline)));
	return rc;
}


static int syncobj_simple(v3da_conn_t *c, uint32_t op, uint32_t handle, uint32_t flags, v3da_syncobj_resp_t *out)
{
	v3da_req_t req;
	v3da_resp_t resp;
	int rc;

	req_init(&req, op);
	req.u.syncobj.handle = handle;
	req.u.syncobj.flags = flags;
	rc = v3da_call(c, &req, &resp);
	if ((rc == 0) && (out != NULL)) {
		*out = resp.u.syncobj;
	}
	return rc;
}


int v3da_syncobj_create(v3da_conn_t *c, uint32_t flags, uint32_t *handle)
{
	v3da_syncobj_resp_t r;
	int rc = syncobj_simple(c, V3DA_OP_SYNCOBJ_CREATE, 0u, flags, &r);

	if (rc == 0) {
		*handle = r.handle;
	}
	return rc;
}


int v3da_syncobj_destroy(v3da_conn_t *c, uint32_t handle)
{
	return syncobj_simple(c, V3DA_OP_SYNCOBJ_DESTROY, handle, 0u, NULL);
}


int v3da_syncobj_reset(v3da_conn_t *c, uint32_t handle)
{
	return syncobj_simple(c, V3DA_OP_SYNCOBJ_RESET, handle, 0u, NULL);
}


int v3da_syncobj_signal(v3da_conn_t *c, uint32_t handle)
{
	return syncobj_simple(c, V3DA_OP_SYNCOBJ_SIGNAL, handle, 0u, NULL);
}


int v3da_syncobj_query(v3da_conn_t *c, uint32_t handle, v3da_syncobj_resp_t *out)
{
	return syncobj_simple(c, V3DA_OP_SYNCOBJ_QUERY, handle, 0u, out);
}


int v3da_syncobj_wait(v3da_conn_t *c, const uint32_t *handles, uint32_t n, uint32_t flags,
	int64_t timeout_ns, uint32_t *first)
{
	v3da_req_t req;
	v3da_resp_t resp;
	uint64_t deadline = now_us() + ((timeout_ns > 0) ? (uint64_t)timeout_ns / 1000u : 0u);
	int forever = (timeout_ns < 0) ? 1 : 0, rc;

	if ((n == 0u) || (n > V3DA_SYNCOBJ_WAIT_MAX)) {
		return -EINVAL;
	}
	do {
		req_init(&req, V3DA_OP_SYNCOBJ_WAIT);
		req.u.syncobj.count = n;
		req.u.syncobj.flags = flags;
		req.u.syncobj.timeout_ms = slice_ms(deadline, forever);
		memcpy(req.u.syncobj.handles, handles, n * sizeof(*handles));
		c->ipc_waits++;
		rc = v3da_call(c, &req, &resp);
	} while ((rc == -ETIMEDOUT) && ((forever != 0) || (now_us() < deadline)));
	if ((rc == 0) && (first != NULL)) {
		*first = resp.u.syncobj.first;
	}
	return rc;
}


int v3da_syncobj_import(v3da_conn_t *c, uint32_t handle, const v3da_fence_t *f)
{
	v3da_req_t req;

	req_init(&req, V3DA_OP_SYNCOBJ_IMPORT);
	req.u.syncobj_import.handle = handle;
	req.u.syncobj_import.fence = *f;
	return v3da_call(c, &req, NULL);
}


int v3da_scanout_info(v3da_conn_t *c, uint64_t pa, uint32_t w, uint32_t h, uint32_t pitch, v3da_scanout_resp_t *out)
{
	v3da_req_t req;
	v3da_resp_t resp;
	int rc;

	req_init(&req, V3DA_OP_SCANOUT_INFO);
	req.u.scanout.pa = pa;
	req.u.scanout.width = w;
	req.u.scanout.height = h;
	req.u.scanout.pitch = pitch;
	rc = v3da_call(c, &req, &resp);
	if ((rc == 0) && (out != NULL)) {
		*out = resp.u.scanout;
	}
	return rc;
}


int v3da_flip(v3da_conn_t *c, uint32_t buf, const v3da_fence_t *after, v3da_flip_resp_t *out)
{
	v3da_req_t req;
	v3da_resp_t resp;
	int rc;

	req_init(&req, V3DA_OP_FLIP);
	req.u.flip.buf = buf;
	if (after != NULL) {
		req.u.flip.flags = V3DA_FLIP_AFTER_FENCE;
		req.u.flip.fence = *after;
	}
	rc = v3da_call(c, &req, &resp);
	if ((rc == 0) && (out != NULL)) {
		*out = resp.u.flip;
	}
	return rc;
}


int v3da_dbg_set_mode(v3da_conn_t *c, uint32_t set, uint32_t mode, uint32_t knobs, v3da_mode_resp_t *out)
{
	v3da_req_t req;
	v3da_resp_t resp;
	int rc;

	req_init(&req, V3DA_OP_DBG_SET_MODE);
	req.u.mode.set = set;
	req.u.mode.mode = mode;
	req.u.mode.knobs = knobs;
	rc = v3da_call(c, &req, &resp);
	if ((rc == 0) && (out != NULL)) {
		*out = resp.u.mode;
	}
	return rc;
}


int v3da_dbg_qstats(v3da_conn_t *c, uint32_t which, uint32_t reset, v3da_resp_t *out)
{
	v3da_req_t req;

	req_init(&req, V3DA_OP_DBG_QSTATS);
	req.u.qstats.which = which;
	req.u.qstats.reset = reset;
	return v3da_call(c, &req, out);
}


int v3da_dbg_irq_mode(v3da_conn_t *c, uint32_t on, v3da_irq_mode_resp_t *out)
{
	v3da_req_t req;
	v3da_resp_t resp;
	int rc;

	req_init(&req, V3DA_OP_DBG_IRQ_MODE);
	req.u.irq_mode.on = on;
	rc = v3da_call(c, &req, &resp);
	if (out != NULL) {
		*out = resp.u.irq_mode;
	}
	return rc;
}


int v3da_dbg_irq_selftest(v3da_conn_t *c, v3da_irq_selftest_resp_t *out)
{
	v3da_req_t req;
	v3da_resp_t resp;
	int rc;

	req_init(&req, V3DA_OP_DBG_IRQ_SELFTEST);
	rc = v3da_call(c, &req, &resp);
	if (rc == 0) {
		*out = resp.u.irq_selftest;
	}
	return rc;
}


int v3da_dbg_bo_checksum(v3da_conn_t *c, uint32_t handle, uint32_t off, uint32_t len, v3da_bo_checksum_resp_t *out)
{
	v3da_req_t req;
	v3da_resp_t resp;
	int rc;

	req_init(&req, V3DA_OP_DBG_BO_CHECKSUM);
	req.u.bo_checksum.handle = handle;
	req.u.bo_checksum.offset = off;
	req.u.bo_checksum.len = len;
	rc = v3da_call(c, &req, &resp);
	if (rc == 0) {
		*out = resp.u.bo_checksum;
	}
	return rc;
}


int v3da_dbg_stats(v3da_conn_t *c, v3da_stats_t *out)
{
	v3da_req_t req;
	v3da_resp_t resp;
	int rc;

	req_init(&req, V3DA_OP_DBG_STATS);
	rc = v3da_call(c, &req, &resp);
	if (rc == 0) {
		*out = resp.u.stats;
	}
	return rc;
}


int v3da_dbg_quit(v3da_conn_t *c, v3da_quit_resp_t *out)
{
	v3da_req_t req;
	v3da_resp_t resp;
	int rc;

	req_init(&req, V3DA_OP_DBG_QUIT);
	rc = v3da_call(c, &req, &resp);
	if (out != NULL) {
		*out = resp.u.quit;
	}
	return rc;
}
