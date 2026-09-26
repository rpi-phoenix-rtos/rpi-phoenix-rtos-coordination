/*
 * Phoenix-RTOS
 *
 * v3dasync-ping - first-contact probe for the new-lane V3D server (/dev/v3d-async)
 *
 * Proves, without submitting any GPU job, that rpi4-v3d-async owns the GPU and that
 * the whole request path works: per-open client id + HELLO, live identity
 * registers, the read-only fence page, BO create/map/checksum/close with
 * quarantine, a deferred FENCE_WAIT answered by the event thread, a bounded
 * timeout, exactly-once answers under concurrency, syncobjs, and the V3D interrupt
 * path (self-test via INT_SET). Pre-registration and predictions:
 * docs/gpu-new-lane/M1-async-render-server.md section 12.
 *
 * Part 2 adds real GPU jobs, each checked in memory by the CPU:
 *   cl-smoke   bin + render clear of a 64x64 RGBA8 raster RT (3 colours)
 *   tfu-smoke  TFU raster -> UBLINEAR-2-column copy of a 16x16 image
 *   csd-smoke  the CSCONST compute kernel (out[0] = 0xC0DE1234)
 *   cl-burst   8 clears queued back to back without waiting (queueing; bin/render
 *              overlap in pipeline mode), all 8 RTs checked
 *   gpu        = cl-smoke tfu-smoke csd-smoke cl-burst qstats
 * and controls: mode-serial, mode-pipeline, qstats, qstats-reset.
 * Job generators: v3da_clgen.c (Mesa's packet packers, verbatim).
 *
 * Usage: v3dasync-ping [all|connect|info|param|fencepage|bo|nopwait|fastpath|
 *                       timeout|many|syncobj|irqtest|stats|irq-on|irq-off|quit|
 *                       cl-smoke|tfu-smoke|csd-smoke|cl-burst|gpu|
 *                       mode-serial|mode-pipeline|qstats|qstats-reset]
 * Every result is one line "V3DAPING <test> key=value ..."; a run ends with
 * "V3DAPING RESULT failures=<n> verdict=PASS|FAIL".
 *
 * Copyright 2026 Phoenix Systems
 * Author: Witold Bołt
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "v3d_drm.h"   /* DRM_V3D_PARAM_*, drm_v3d_submit_* (filled by the generators) */
#include "libv3da-client.h"
#include "v3da_clgen.h"
#include "v3da_regs.h"


static v3da_conn_t conn;
static int failures;


static uint64_t now_us(void)
{
	struct timespec ts;

	(void)clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}


static void verdict(int ok)
{
	if (ok == 0) {
		failures++;
	}
}


static uint32_t fnv(const volatile uint8_t *p, uint32_t len)
{
	uint32_t h = 2166136261u, i;

	for (i = 0u; i < len; i++) {
		h ^= p[i];
		h *= 16777619u;
	}
	return h;
}


/* ------------------------------------------------------------------------- */

static int t_info(void)
{
	v3da_info_t in;
	int rc, match;

	rc = v3da_get_info(&conn, &in);
	if (rc != 0) {
		printf("V3DAPING info rc=%d\n", rc);
		verdict(0);
		return rc;
	}
	match = (in.ident[0] == V3D_EXPECT_CORE0_IDENT0) && (in.ident[1] == V3D_EXPECT_CORE0_IDENT1) &&
		(in.ident[2] == V3D_EXPECT_CORE0_IDENT2) && (in.ident[3] == V3D_EXPECT_HUB_UIFCFG) &&
		(in.ident[4] == V3D_EXPECT_HUB_IDENT1) && (in.ident[5] == V3D_EXPECT_HUB_IDENT2) &&
		(in.ident[6] == V3D_EXPECT_HUB_IDENT3);
	printf("V3DAPING info ident_match=%d core0=0x%08x hub1=0x%08x irq_mode=%u irq=%u clk_rate_hz=%u clk_meas_hz=%u "
		"clients=%u\n", match, in.ident[0], in.ident[4], in.irq_mode, in.irq_num, in.clk_rate_hz, in.clk_meas_hz,
		in.nclients);
	verdict(match);
	return 0;
}


static void t_param(void)
{
	static const struct { uint32_t p; uint64_t want; const char *n; } tab[] = {
		{ DRM_V3D_PARAM_V3D_CORE0_IDENT0, V3D_EXPECT_CORE0_IDENT0, "core0_ident0" },
		{ DRM_V3D_PARAM_V3D_HUB_IDENT1, V3D_EXPECT_HUB_IDENT1, "hub_ident1" },
		{ DRM_V3D_PARAM_V3D_UIFCFG, V3D_EXPECT_HUB_UIFCFG, "uifcfg" },
		{ DRM_V3D_PARAM_SUPPORTS_TFU, 1u, "tfu" },
		{ DRM_V3D_PARAM_SUPPORTS_CSD, 1u, "csd" },
		{ DRM_V3D_PARAM_SUPPORTS_MULTISYNC_EXT, 1u, "multisync" },
		{ DRM_V3D_PARAM_SUPPORTS_CPU_QUEUE, 1u, "cpu_queue" },
	};
	unsigned i, bad = 0u;
	uint64_t v;
	int rc;

	for (i = 0u; i < sizeof(tab) / sizeof(tab[0]); i++) {
		rc = v3da_get_param(&conn, tab[i].p, &v);
		if ((rc != 0) || (v != tab[i].want)) {
			bad++;
			printf("V3DAPING param_bad name=%s rc=%d value=0x%llx want=0x%llx\n", tab[i].n, rc,
				(unsigned long long)v, (unsigned long long)tab[i].want);
		}
	}
	printf("V3DAPING param checked=%u bad=%u\n", (unsigned)(sizeof(tab) / sizeof(tab[0])), bad);
	verdict(bad == 0u);
}


static void t_fencepage(void)
{
	const volatile v3da_fence_page_t *fp = conn.fp;
	uint64_t hb0, hb1;
	int magic_ok;

	magic_ok = (fp->hdr.magic == V3DA_FENCE_MAGIC) && (fp->hdr.version == V3DA_PROTO_VERSION) &&
		(fp->hdr.nslots == V3DA_FENCE_NSLOTS);
	hb0 = __atomic_load_n(&fp->hdr.heartbeat, __ATOMIC_ACQUIRE);
	usleep(250000);
	hb1 = __atomic_load_n(&fp->hdr.heartbeat, __ATOMIC_ACQUIRE);
	printf("V3DAPING fencepage magic_ok=%d heartbeat_moves=%d hb0=%llu hb1=%llu flags=0x%x slot=%u gen=%llu "
		"server_pid=%u pa=0x%llx\n", magic_ok, (hb1 != hb0) ? 1 : 0, (unsigned long long)hb0,
		(unsigned long long)hb1, fp->hdr.flags, conn.hello.slot,
		(unsigned long long)fp->slot[conn.hello.slot].gen, fp->hdr.server_pid,
		(unsigned long long)conn.hello.fence_page.addr);
	verdict(magic_ok && (hb1 != hb0));
}


static void t_bo(void)
{
	const uint32_t size = 64u * 1024u;
	v3da_bo_create_resp_t bo, bo2;
	v3da_bo_checksum_resp_t cs;
	v3da_stats_t s0, s1;
	volatile uint32_t *p, *p2;
	uint32_t i, csum, gpuva = 0u;
	int zeroed = 1, reuse_zeroed = 1, rc, rc_close, rc_stale, rc_cs;

	(void)v3da_dbg_stats(&conn, &s0);
	rc = v3da_bo_create(&conn, size, 0u, &bo);
	if (rc != 0) {
		printf("V3DAPING bo create=%d\n", rc);
		verdict(0);
		return;
	}
	p = v3da_map(&bo.mem, 1);
	if (p == NULL) {
		printf("V3DAPING bo create=0 map=FAILED pa=0x%llx\n", (unsigned long long)bo.mem.addr);
		verdict(0);
		(void)v3da_bo_close(&conn, bo.handle);
		return;
	}
	for (i = 0u; i < size / 4u; i++) {
		if (p[i] != 0u) {
			zeroed = 0;
			break;
		}
	}
	for (i = 0u; i < size / 4u; i++) {
		p[i] = 0x9e3779b9u * (i + 1u) ^ bo.handle;
	}
	__asm__ volatile("dsb sy" ::: "memory");
	csum = fnv((const volatile uint8_t *)p, size);
	rc_cs = v3da_dbg_bo_checksum(&conn, bo.handle, 0u, size, &cs);
	v3da_unmap((void *)p, &bo.mem);
	rc_close = v3da_bo_close(&conn, bo.handle);
	rc_stale = v3da_bo_offset(&conn, bo.handle, &gpuva);
	(void)v3da_dbg_stats(&conn, &s1);

	/* Same size again: must come from the pool and be zeroed again. */
	rc = v3da_bo_create(&conn, size, 0u, &bo2);
	if (rc == 0) {
		p2 = v3da_map(&bo2.mem, 0);
		if (p2 != NULL) {
			for (i = 0u; i < size / 4u; i++) {
				if (p2[i] != 0u) {
					reuse_zeroed = 0;
					break;
				}
			}
			v3da_unmap((void *)p2, &bo2.mem);
		}
		else {
			reuse_zeroed = 0;
		}
		(void)v3da_bo_close(&conn, bo2.handle);
	}

	printf("V3DAPING bo create=0 handle=%u gpuva=0x%08x size=%u pa=0x%llx cache=%u zeroed=%d sum_match=%d "
		"cs_rc=%d close=%d stale_handle=%d quarantine_passed=%u pooled=%u to_kernel=%u reuse=%d "
		"reuse_same_pa=%d reuse_zeroed=%d\n",
		bo.handle, bo.gpuva, bo.size, (unsigned long long)bo.mem.addr, bo.mem.cache, zeroed,
		((rc_cs == 0) && (cs.sum == csum)) ? 1 : 0, rc_cs, rc_close, rc_stale,
		s1.bo_quarantine_passed - s0.bo_quarantine_passed, s1.bos_pooled, s1.pages_to_kernel, rc,
		((rc == 0) && (bo2.mem.addr == bo.mem.addr)) ? 1 : 0, reuse_zeroed);
	verdict(zeroed && (rc_cs == 0) && (cs.sum == csum) && (rc_close == 0) && (rc_stale == -EINVAL) &&
		((s1.bo_quarantine_passed - s0.bo_quarantine_passed) >= 1u) && (s1.pages_to_kernel == 0u) &&
		(rc == 0) && reuse_zeroed);
}


static void t_nopwait(void)
{
	v3da_fence_t f;
	v3da_fence_wait_resp_t wr;
	uint64_t t0, t1;
	int rc, early, after;

	memset(&wr, 0, sizeof(wr));
	rc = v3da_submit_nop(&conn, 20000u, &f);
	if (rc != 0) {
		printf("V3DAPING nopwait submit=%d\n", rc);
		verdict(0);
		return;
	}
	early = v3da_fence_signaled(&conn, &f);
	t0 = now_us();
	rc = v3da_fence_wait_once(&conn, &f, 1000u, &wr);
	t1 = now_us();
	after = v3da_fence_signaled(&conn, &f);
	printf("V3DAPING nopwait ok=%d rc=%d early=%d after=%d wait_ms=%llu seq=%llu completed=%llu echo_ok=%d\n",
		((rc == 0) && (early == 0) && (after == 1)) ? 1 : 0, rc, early, after,
		(unsigned long long)((t1 - t0) / 1000u), (unsigned long long)f.seqno,
		(unsigned long long)wr.completed, (wr.echo == (uint32_t)f.seqno) ? 1 : 0);
	verdict((rc == 0) && (early == 0) && (after == 1) && (wr.echo == (uint32_t)f.seqno));
}


static void t_fastpath(void)
{
	v3da_fence_t f;
	uint64_t t0, t1;
	uint32_t ipc0 = conn.ipc_waits;
	int rc, sig = 0;

	rc = v3da_submit_nop(&conn, 0u, &f);
	t0 = now_us();
	while ((rc == 0) && ((now_us() - t0) < 1000000u)) {
		if (v3da_fence_signaled(&conn, &f) != 0) {
			sig = 1;
			break;
		}
	}
	t1 = now_us();
	printf("V3DAPING fastpath ok=%d rc=%d ipc_waits=%u spin_us=%llu\n", ((rc == 0) && sig) ? 1 : 0, rc,
		conn.ipc_waits - ipc0, (unsigned long long)(t1 - t0));
	verdict((rc == 0) && sig && (conn.ipc_waits == ipc0));
}


static void t_timeout(void)
{
	v3da_fence_t f;
	uint64_t t0, t1;
	int rc, rc2 = -1;

	rc = v3da_submit_nop(&conn, 500000u, &f);
	if (rc != 0) {
		printf("V3DAPING timeout submit=%d\n", rc);
		verdict(0);
		return;
	}
	t0 = now_us();
	rc = v3da_fence_wait_once(&conn, &f, 100u, NULL);
	t1 = now_us();
	rc2 = v3da_fence_wait(&conn, &f, 2000000000LL);
	printf("V3DAPING timeout rc=%d elapsed_ms=%llu late_ok=%d\n", rc, (unsigned long long)((t1 - t0) / 1000u),
		(rc2 == 0) ? 1 : 0);
	verdict((rc == -ETIMEDOUT) && ((t1 - t0) >= 90000u) && ((t1 - t0) < 300000u) && (rc2 == 0));
}


#define MANY_THREADS 4
#define MANY_PER     20

static struct {
	pthread_mutex_t lock;
	unsigned ok, bad_token, errs;
} many;


static void *many_thread(void *arg)
{
	unsigned seed = (unsigned)(uintptr_t)arg * 2654435761u, i;
	v3da_fence_t f;
	v3da_fence_wait_resp_t wr;
	int rc;

	for (i = 0u; i < MANY_PER; i++) {
		seed = seed * 1103515245u + 12345u;
		rc = v3da_submit_nop(&conn, (seed >> 8) % 10000u, &f);
		if (rc == 0) {
			memset(&wr, 0, sizeof(wr));
			rc = v3da_fence_wait_once(&conn, &f, 1500u, &wr);
		}
		pthread_mutex_lock(&many.lock);
		if (rc != 0) {
			many.errs++;
		}
		else if ((wr.echo != (uint32_t)f.seqno) || (wr.completed < f.seqno) ||
				(v3da_fence_signaled(&conn, &f) == 0)) {
			many.bad_token++;   /* answered with someone else's reply, or too early */
		}
		else {
			many.ok++;
		}
		pthread_mutex_unlock(&many.lock);
	}
	return NULL;
}


static void t_many(void)
{
	pthread_t th[MANY_THREADS];
	v3da_stats_t s;
	int i;

	memset(&many, 0, sizeof(many));
	pthread_mutex_init(&many.lock, NULL);
	for (i = 0; i < MANY_THREADS; i++) {
		pthread_create(&th[i], NULL, many_thread, (void *)(uintptr_t)(i + 1));
	}
	for (i = 0; i < MANY_THREADS; i++) {
		pthread_join(th[i], NULL);
	}
	(void)v3da_dbg_stats(&conn, &s);
	printf("V3DAPING many ok=%u/%u bad_token=%u errs=%u parked_max=%u parked_now=%u\n", many.ok,
		MANY_THREADS * MANY_PER, many.bad_token, many.errs, s.parked_max, s.parked);
	verdict((many.ok == MANY_THREADS * MANY_PER) && (many.bad_token == 0u) && (s.parked == 0u));
}


static void t_syncobj(void)
{
	uint32_t h = 0u, h2 = 0u, first = 99u;
	int rc_c, rc_wu, rc_s, rc_w, rc_r, rc_we, rc_d, rc_c2, rc_w2;
	v3da_syncobj_resp_t q;

	rc_c = v3da_syncobj_create(&conn, 0u, &h);
	rc_wu = v3da_syncobj_wait(&conn, &h, 1u, V3DA_SYNCOBJ_WAIT_FOR_SUBMIT, 100000000LL, NULL);
	rc_s = v3da_syncobj_signal(&conn, h);
	rc_w = v3da_syncobj_wait(&conn, &h, 1u, 0u, 100000000LL, &first);
	rc_r = v3da_syncobj_reset(&conn, h);
	rc_we = v3da_syncobj_wait(&conn, &h, 1u, 0u, 100000000LL, NULL);
	(void)v3da_syncobj_query(&conn, h, &q);
	rc_d = v3da_syncobj_destroy(&conn, h);
	rc_c2 = v3da_syncobj_create(&conn, V3DA_SYNCOBJ_CREATE_SIGNALED, &h2);
	rc_w2 = v3da_syncobj_wait(&conn, &h2, 1u, V3DA_SYNCOBJ_WAIT_ALL, 0LL, NULL);
	(void)v3da_syncobj_destroy(&conn, h2);
	printf("V3DAPING syncobj create=%d wait_unsignalled=%d signal=%d wait=%d first=%u reset=%d wait_empty=%d "
		"state_after_reset=%u destroy=%d create_signaled=%d wait_signaled=%d\n",
		rc_c, rc_wu, rc_s, rc_w, first, rc_r, rc_we, q.state, rc_d, rc_c2, rc_w2);
	verdict((rc_c == 0) && (rc_wu == -ETIMEDOUT) && (rc_s == 0) && (rc_w == 0) && (first == 0u) && (rc_r == 0) &&
		(rc_we == -EINVAL) && (rc_d == 0) && (rc_c2 == 0) && (rc_w2 == 0));
}


static void t_irqtest(void)
{
	v3da_irq_selftest_resp_t st;
	int rc, ok, inconclusive = 0;

	memset(&st, 0, sizeof(st));
	rc = v3da_dbg_irq_selftest(&conn, &st);
	if (st.mode == 0u) {
		/* poll mode: INT_SET must latch in the raw status and reach the event thread */
		if ((st.core_sts_seen == 0u) || (st.hub_sts_seen == 0u)) {
			inconclusive = 1;   /* the self-test's premise failed: not an IRQ-path verdict */
		}
		ok = (rc == 0) && ((inconclusive != 0) || ((st.core_events >= 1u) && (st.hub_events >= 1u)));
	}
	else {
		ok = (rc == 0) && (st.handler_delta >= 1u) && (st.core_events >= 1u) && (st.hub_events >= 1u) &&
			(st.storm == 0u);
	}
	printf("V3DAPING irqtest rc=%d mode=%s core_sts_seen=%u hub_sts_seen=%u handler_delta=%u core_events=%u "
		"hub_events=%u storm=%u core_msk=0x%08x hub_msk=0x%08x inconclusive=%d ok=%d\n",
		rc, (st.mode != 0u) ? "irq" : "poll", st.core_sts_seen, st.hub_sts_seen, st.handler_delta,
		st.core_events, st.hub_events, st.storm, st.core_msk, st.hub_msk, inconclusive, ok);
	verdict(ok);
}


static void t_stats(void)
{
	v3da_stats_t s;
	int rc = v3da_dbg_stats(&conn, &s);

	printf("V3DAPING stats rc=%d clients=%u bos_live=%u quarantined=%u pooled=%u passed=%u to_kernel=%u parked=%u "
		"parked_max=%u irq_count=%u tlb_flushes=%u loops=%u timeouts=%u nops=%u resets=%u ipc_waits=%u\n",
		rc, s.clients, s.bos_live, s.bos_quarantined, s.bos_pooled, s.bo_quarantine_passed, s.pages_to_kernel,
		s.parked, s.parked_max, s.irq_count, s.tlb_flushes, s.loops, s.timeouts, s.nops_done, s.resets,
		conn.ipc_waits);
	verdict((rc == 0) && (s.parked == 0u) && (s.pages_to_kernel == 0u));
}


static void t_irqmode(uint32_t on)
{
	v3da_irq_mode_resp_t r;
	int rc;

	memset(&r, 0, sizeof(r));
	rc = v3da_dbg_irq_mode(&conn, on, &r);
	printf("V3DAPING irqmode rc=%d want=%s mode=%s irq=%u irq_rc=%d\n", rc, (on != 0u) ? "irq" : "poll",
		(r.mode != 0u) ? "irq" : "poll", r.irq, r.rc);
	verdict((rc == 0) && (r.mode == on));
}


static void t_quit(void)
{
	v3da_quit_resp_t r;
	int rc;

	memset(&r, 0, sizeof(r));
	rc = v3da_dbg_quit(&conn, &r);
	printf("V3DAPING quit rc=%d parked=%u inflight=%u\n", rc, r.parked, r.inflight);
	verdict(rc == 0);
}


/* ------------------------------------------------------------------------- */
/* Part 2: GPU jobs                                                           */
/* ------------------------------------------------------------------------- */

#define JOB_WAIT_NS 2000000000LL

typedef struct {
	v3da_bo_create_resp_t r;
	volatile uint32_t *cpu;
} tbo_t;


static int tbo_new(tbo_t *b, uint32_t size)
{
	int rc = v3da_bo_create(&conn, size, 0u, &b->r);

	if (rc != 0) {
		b->cpu = NULL;
		return rc;
	}
	b->cpu = v3da_map(&b->r.mem, 1);
	if (b->cpu == NULL) {
		(void)v3da_bo_close(&conn, b->r.handle);
		return -ENOMEM;
	}
	return 0;
}


static void tbo_free(tbo_t *b)
{
	if (b->cpu != NULL) {
		v3da_unmap((void *)b->cpu, &b->r.mem);
		(void)v3da_bo_close(&conn, b->r.handle);
		b->cpu = NULL;
	}
}


static v3da_clgen_buf_t tbuf(tbo_t *b)
{
	v3da_clgen_buf_t g;

	g.cpu = (void *)b->cpu;
	g.gpuva = b->r.gpuva;
	g.size = b->r.size;
	return g;
}


/* Wait for a fence and time it. Returns the wait rc; *err = fence error flag. */
static int job_wait(const v3da_fence_t *f, uint64_t *us, int *err)
{
	uint64_t t0 = now_us();
	int rc = v3da_fence_wait(&conn, f, JOB_WAIT_NS);

	*us = now_us() - t0;
	*err = v3da_fence_error(&conn, f);
	return rc;
}


/* One 64x64 clear job: BOs, CLs, submit. The caller waits and checks. */
typedef struct {
	tbo_t bcl, rcl, ta, ts, rt;
	uint32_t w, h, colour;
	v3da_submit_resp_t fences;
	int rc;
} clear_job_t;


static int clear_prepare(clear_job_t *j, uint32_t w, uint32_t h, uint32_t colour)
{
	uint32_t bsz, rsz, tasz, tssz, i;
	struct drm_v3d_submit_cl s;
	v3da_clgen_buf_t b, r;
	v3da_cl_desc_t d;
	uint32_t bos[5];
	int rc;

	memset(j, 0, sizeof(*j));
	j->w = w;
	j->h = h;
	j->colour = colour;
	rc = v3da_clgen_clear_sizes(w, h, &bsz, &rsz, &tasz, &tssz);
	if (rc == 0) rc = tbo_new(&j->bcl, bsz);
	if (rc == 0) rc = tbo_new(&j->rcl, rsz);
	if (rc == 0) rc = tbo_new(&j->ta, tasz);
	if (rc == 0) rc = tbo_new(&j->ts, tssz);
	if (rc == 0) rc = tbo_new(&j->rt, V3DA_CLGEN_CLEAR_RT_SIZE(w, h));
	if (rc != 0) {
		return rc;
	}
	for (i = 0u; i < w * h; i++) {
		j->rt.cpu[i] = 0xdeadbeefu;   /* sentinel: "not written" */
	}
	b = tbuf(&j->bcl);
	r = tbuf(&j->rcl);
	rc = v3da_clgen_clear(w, h, j->rt.r.gpuva, colour, &b, &r, j->ta.r.gpuva, j->ta.r.size, j->ts.r.gpuva, &s);
	if (rc != 0) {
		return rc;
	}
	__asm__ volatile("dsb sy" ::: "memory");
	d.bcl_start = s.bcl_start;
	d.bcl_end = s.bcl_end;
	d.rcl_start = s.rcl_start;
	d.rcl_end = s.rcl_end;
	d.qma = s.qma;
	d.qms = s.qms;
	d.qts = s.qts;
	d.flags = 0u;
	bos[0] = j->bcl.r.handle;
	bos[1] = j->rcl.r.handle;
	bos[2] = j->ta.r.handle;
	bos[3] = j->ts.r.handle;
	bos[4] = j->rt.r.handle;
	return v3da_submit(&conn, V3DA_OP_SUBMIT_CL, &d, sizeof(d), bos, 5u, NULL, 0u, NULL, 0u, &j->fences);
}


/* Count RT pixels that differ from the clear colour. */
static uint32_t clear_check(const clear_job_t *j, uint32_t *first_bad)
{
	uint32_t i, bad = 0u;

	*first_bad = 0u;
	for (i = 0u; i < j->w * j->h; i++) {
		if (j->rt.cpu[i] != j->colour) {
			if (bad++ == 0u) {
				*first_bad = j->rt.cpu[i];
			}
		}
	}
	return bad;
}


static void clear_free(clear_job_t *j)
{
	tbo_free(&j->rt);
	tbo_free(&j->ts);
	tbo_free(&j->ta);
	tbo_free(&j->rcl);
	tbo_free(&j->bcl);
}


static void t_cl_smoke(void)
{
	static const uint32_t colours[3] = { 0x80402010u, 0xff00ff00u, 0x12345678u };
	clear_job_t j;
	uint64_t us;
	uint32_t bad, first;
	int k, rc, err, bw;

	for (k = 0; k < 3; k++) {
		rc = clear_prepare(&j, 64u, 64u, colours[k]);
		if (rc != 0) {
			printf("V3DAPING cl-smoke n=%d prepare_rc=%d\n", k, rc);
			verdict(0);
			clear_free(&j);
			continue;
		}
		rc = job_wait(&j.fences.last, &us, &err);
		bad = clear_check(&j, &first);
		/* implicit sync: the RT's last user is this render, which has completed */
		bw = v3da_bo_wait(&conn, j.rt.r.handle, 0);
		printf("V3DAPING cl-smoke n=%d colour=0x%08x wait_rc=%d fence_err=%d us=%llu bin_seq=%llu render_seq=%llu "
			"bad_px=%u/%u first_bad=0x%08x bo_wait=%d ok=%d\n", k, colours[k], rc, err, (unsigned long long)us,
			(unsigned long long)j.fences.first.seqno, (unsigned long long)j.fences.last.seqno, bad, j.w * j.h,
			first, bw, ((rc == 0) && (err == 0) && (bad == 0u) && (bw == 0)) ? 1 : 0);
		verdict((rc == 0) && (err == 0) && (bad == 0u) && (bw == 0));
		clear_free(&j);
	}
}


#define BURST 8

static void t_cl_burst(void)
{
	static clear_job_t jobs[BURST];
	uint64_t t0, us;
	uint32_t bad = 0u, first, b, nsub = 0u;
	int k, rc = 0, err = 0, e;

	t0 = now_us();
	for (k = 0; k < BURST; k++) {
		jobs[k].rc = clear_prepare(&jobs[k], 64u, 64u, 0x01010101u * (uint32_t)(k + 1));
		if (jobs[k].rc == 0) {
			nsub++;
		}
	}
	for (k = 0; k < BURST; k++) {
		if (jobs[k].rc != 0) {
			continue;
		}
		e = 0;
		if (job_wait(&jobs[k].fences.last, &us, &e) != 0) {
			rc++;
		}
		err += e;
		b = clear_check(&jobs[k], &first);
		bad += b;
	}
	us = now_us() - t0;
	for (k = 0; k < BURST; k++) {
		clear_free(&jobs[k]);
	}
	printf("V3DAPING cl-burst jobs=%u/%d wait_fail=%d fence_err=%d bad_px=%u total_us=%llu ok=%d\n", nsub, BURST, rc, err,
		bad, (unsigned long long)us, ((nsub == BURST) && (rc == 0) && (err == 0) && (bad == 0u)) ? 1 : 0);
	verdict((nsub == BURST) && (rc == 0) && (err == 0) && (bad == 0u));
}


static void t_tfu_smoke(void)
{
	const uint32_t w = 16u, h = 16u;
	tbo_t src, dst;
	struct drm_v3d_submit_tfu t;
	v3da_tfu_desc_t d;
	v3da_submit_resp_t f;
	uint32_t x, y, bad = 0u, bos[2], first = 0u;
	uint64_t us = 0u;
	int rc, err = 0;

	memset(&src, 0, sizeof(src));
	memset(&dst, 0, sizeof(dst));
	rc = tbo_new(&src, v3da_clgen_tfu_src_size(w, h));
	if (rc == 0) rc = tbo_new(&dst, v3da_clgen_tfu_out_size(w, h));
	if (rc == 0) {
		for (y = 0u; y < h; y++) {
			for (x = 0u; x < w; x++) {
				src.cpu[y * w + x] = v3da_clgen_tfu_pattern(x, y);
			}
		}
		for (x = 0u; x < v3da_clgen_tfu_out_size(w, h) / 4u; x++) {
			dst.cpu[x] = 0u;
		}
		__asm__ volatile("dsb sy" ::: "memory");
		rc = v3da_clgen_tfu(w, h, src.r.gpuva, src.r.size, dst.r.gpuva, dst.r.size, &t);
	}
	if (rc == 0) {
		d.icfg = t.icfg;
		d.iia = t.iia;
		d.iis = t.iis;
		d.ica = t.ica;
		d.iua = t.iua;
		d.ioa = t.ioa;
		d.ios = t.ios;
		memcpy(d.coef, t.coef, sizeof(d.coef));
		bos[0] = dst.r.handle;
		bos[1] = src.r.handle;
		rc = v3da_submit(&conn, V3DA_OP_SUBMIT_TFU, &d, sizeof(d), bos, 2u, NULL, 0u, NULL, 0u, &f);
	}
	if (rc == 0) {
		rc = job_wait(&f.last, &us, &err);
		for (y = 0u; y < h; y++) {
			for (x = 0u; x < w; x++) {
				if (dst.cpu[v3da_clgen_tfu_out_offset(x, y) / 4u] != src.cpu[y * w + x]) {
					if (bad++ == 0u) {
						first = dst.cpu[v3da_clgen_tfu_out_offset(x, y) / 4u];
					}
				}
			}
		}
	}
	printf("V3DAPING tfu-smoke rc=%d fence_err=%d us=%llu bad_px=%u/%u first_bad=0x%08x out0=0x%08x ok=%d\n", rc, err,
		(unsigned long long)us, bad, w * h, first, (dst.cpu != NULL) ? dst.cpu[0] : 0u,
		((rc == 0) && (err == 0) && (bad == 0u)) ? 1 : 0);
	verdict((rc == 0) && (err == 0) && (bad == 0u));
	tbo_free(&dst);
	tbo_free(&src);
}


static void t_csd_smoke(void)
{
	tbo_t sh, un, out;
	v3da_clgen_buf_t bs, bu;
	v3da_csd_desc_t d;
	v3da_submit_resp_t f;
	uint32_t bos[3], i, others = 0u;
	uint64_t us = 0u;
	int rc, err = 0;

	memset(&sh, 0, sizeof(sh));
	memset(&un, 0, sizeof(un));
	memset(&out, 0, sizeof(out));
	rc = tbo_new(&sh, 4096u);
	if (rc == 0) rc = tbo_new(&un, 4096u);
	if (rc == 0) rc = tbo_new(&out, 4096u);
	if (rc == 0) {
		for (i = 0u; i < 1024u; i++) {
			out.cpu[i] = 0xeeeeeeeeu;
		}
		bs = tbuf(&sh);
		bu = tbuf(&un);
		memset(&d, 0, sizeof(d));
		rc = v3da_clgen_csd(&bs, &bu, out.r.gpuva, d.cfg);
		__asm__ volatile("dsb sy" ::: "memory");
	}
	if (rc == 0) {
		bos[0] = sh.r.handle;
		bos[1] = un.r.handle;
		bos[2] = out.r.handle;
		rc = v3da_submit(&conn, V3DA_OP_SUBMIT_CSD, &d, sizeof(d), bos, 3u, NULL, 0u, NULL, 0u, &f);
	}
	if (rc == 0) {
		rc = job_wait(&f.last, &us, &err);
		for (i = 1u; i < 1024u; i++) {
			if (out.cpu[i] != 0xeeeeeeeeu) {
				others++;
			}
		}
	}
	printf("V3DAPING csd-smoke rc=%d fence_err=%d us=%llu out0=0x%08x others_written=%u ok=%d\n", rc, err,
		(unsigned long long)us, (out.cpu != NULL) ? out.cpu[0] : 0u, others,
		((rc == 0) && (err == 0) && (out.cpu != NULL) && (out.cpu[0] == V3DA_CLGEN_CSD_VALUE)) ? 1 : 0);
	verdict((rc == 0) && (err == 0) && (out.cpu != NULL) && (out.cpu[0] == V3DA_CLGEN_CSD_VALUE));
	tbo_free(&out);
	tbo_free(&un);
	tbo_free(&sh);
}


static void t_mode(uint32_t mode)
{
	v3da_mode_resp_t r;
	int rc;

	memset(&r, 0, sizeof(r));
	rc = v3da_dbg_set_mode(&conn, V3DA_SET_MODE, mode, 0u, &r);
	printf("V3DAPING mode rc=%d want=%s mode=%s knobs=0x%02x\n", rc, (mode == V3DA_MODE_SERIAL) ? "serial" : "pipeline",
		(r.mode == V3DA_MODE_SERIAL) ? "serial" : "pipeline", r.knobs);
	verdict((rc == 0) && (r.mode == mode));
}


static void t_qstats(uint32_t reset)
{
	static const char *const qn[4] = { "bin", "render", "tfu", "csd" };
	v3da_resp_t r;
	uint32_t q;
	int rc;

	for (q = 0u; q < 4u; q++) {
		memset(&r, 0, sizeof(r));
		rc = v3da_dbg_qstats(&conn, q, 0u, &r);
		printf("V3DAPING qstats q=%s rc=%d jobs=%u errors=%u busy_us=%llu wait_us=%llu max_us=%u oom=%u pending=%u "
			"active=%u\n", qn[q], rc, r.u.qstats_q.jobs, r.u.qstats_q.errors,
			(unsigned long long)r.u.qstats_q.busy_us, (unsigned long long)r.u.qstats_q.wait_us, r.u.qstats_q.max_us,
			r.u.qstats_q.oom, r.u.qstats_q.pending, r.u.qstats_q.active);
	}
	memset(&r, 0, sizeof(r));
	rc = v3da_dbg_qstats(&conn, V3DA_QSTATS_GLOBAL, reset, &r);
	printf("V3DAPING qstats q=global rc=%d window_us=%llu any_busy_us=%llu overlap_us=%llu mode=%s knobs=0x%02x "
		"wedges=%u resets=%u ovf_free=%u/%u starved=%u flips=%u reset=%u\n", rc,
		(unsigned long long)r.u.qstats_g.window_us, (unsigned long long)r.u.qstats_g.any_busy_us,
		(unsigned long long)r.u.qstats_g.overlap_us, (r.u.qstats_g.mode == V3DA_MODE_SERIAL) ? "serial" : "pipeline",
		r.u.qstats_g.knobs, r.u.qstats_g.wedges, r.u.qstats_g.resets, r.u.qstats_g.ovf_free, r.u.qstats_g.ovf_total,
		r.u.qstats_g.ovf_starved, r.u.qstats_g.flips, reset);
	verdict((rc == 0) && (r.u.qstats_g.ovf_free == r.u.qstats_g.ovf_total));
}


/* ------------------------------------------------------------------------- */

int main(int argc, char **argv)
{
	const char *cmd = (argc > 1) ? argv[1] : "all";
	uint64_t t0 = now_us();
	int rc, all = (strcmp(cmd, "all") == 0) ? 1 : 0;

	setvbuf(stdout, NULL, _IOLBF, 0);

	rc = v3da_connect(&conn);
	printf("V3DAPING connect ok=%d rc=%d id=%u slot=%u gen=%u proto=%u server_pid=%u flags=0x%x wait=%s\n",
		(rc == 0) ? 1 : 0, rc, conn.hello.client_id, conn.hello.slot, conn.hello.slot_gen, conn.hello.proto,
		conn.hello.server_pid, conn.hello.flags, (conn.wait_poll != 0) ? "poll" : "park");
	if (rc != 0) {
		printf("V3DAPING RESULT failures=1 verdict=FAIL\n");
		return 1;
	}

	if (all || (strcmp(cmd, "info") == 0)) {
		(void)t_info();
	}
	if (all || (strcmp(cmd, "param") == 0)) {
		t_param();
	}
	if (all || (strcmp(cmd, "fencepage") == 0)) {
		t_fencepage();
	}
	if (all || (strcmp(cmd, "bo") == 0)) {
		t_bo();
	}
	if (all || (strcmp(cmd, "nopwait") == 0)) {
		t_nopwait();
	}
	if (all || (strcmp(cmd, "fastpath") == 0)) {
		t_fastpath();
	}
	if (all || (strcmp(cmd, "timeout") == 0)) {
		t_timeout();
	}
	if (all || (strcmp(cmd, "many") == 0)) {
		t_many();
	}
	if (all || (strcmp(cmd, "syncobj") == 0)) {
		t_syncobj();
	}
	if (all || (strcmp(cmd, "irqtest") == 0)) {
		t_irqtest();
	}
	if (all || (strcmp(cmd, "stats") == 0)) {
		t_stats();
	}
	{
		int gpu = (strcmp(cmd, "gpu") == 0) ? 1 : 0;

		if (gpu || (strcmp(cmd, "cl-smoke") == 0)) {
			t_cl_smoke();
		}
		if (gpu || (strcmp(cmd, "tfu-smoke") == 0)) {
			t_tfu_smoke();
		}
		if (gpu || (strcmp(cmd, "csd-smoke") == 0)) {
			t_csd_smoke();
		}
		if (gpu || (strcmp(cmd, "cl-burst") == 0)) {
			t_cl_burst();
		}
		if (gpu || (strcmp(cmd, "qstats") == 0)) {
			t_qstats(0u);
		}
	}
	if (strcmp(cmd, "qstats-reset") == 0) {
		t_qstats(1u);
	}
	if (strcmp(cmd, "mode-serial") == 0) {
		t_mode(V3DA_MODE_SERIAL);
	}
	if (strcmp(cmd, "mode-pipeline") == 0) {
		t_mode(V3DA_MODE_PIPELINE);
	}
	if (strcmp(cmd, "irq-on") == 0) {
		t_irqmode(1u);
	}
	if (strcmp(cmd, "irq-off") == 0) {
		t_irqmode(0u);
	}
	if (strcmp(cmd, "quit") == 0) {
		t_quit();
		/* the server is gone: skip the close handshake */
		printf("V3DAPING RESULT failures=%d verdict=%s elapsed_ms=%llu\n", failures,
			(failures == 0) ? "PASS" : "FAIL", (unsigned long long)((now_us() - t0) / 1000u));
		return (failures == 0) ? 0 : 1;
	}

	v3da_disconnect(&conn);
	printf("V3DAPING RESULT failures=%d verdict=%s elapsed_ms=%llu\n", failures, (failures == 0) ? "PASS" : "FAIL",
		(unsigned long long)((now_us() - t0) / 1000u));
	return (failures == 0) ? 0 : 1;
}
