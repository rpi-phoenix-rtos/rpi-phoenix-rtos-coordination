/*
 * Phoenix-RTOS
 *
 * Raspberry Pi 4 (BCM2711) V3D 4.2 asynchronous render server - jobs
 *
 * Submission, the per-queue FIFOs, the scheduler, the kick/complete sequences of
 * every hardware queue, binner-overflow memory, the watchdog and GPU reset, and
 * the transitional present family (firmware pan). Everything here runs with
 * srv.lock held; completions only ever run on the event thread.
 *
 * The cache/TLB maintenance around each job is the old synchronous lane's
 * sequence, step by step (docs/gpu-new-lane/E2-stk-submit-breakdown.md steps
 * 1-16; gpu/rpi4-v3d/v3d_gpu.c ioc_submit_cl / ioc_submit_tfu / v3d_gpu_submitCsd,
 * themselves copies of mesa/v3d_phoenix_winsys.c). What changed is only WHO waits:
 * the old code spun on CTL_INT_STS / HUB_INT_STS inside the caller's thread; here
 * the kick returns and the completion arrives through the event thread (IRQ or
 * poll, same code). Every step the old lane needed is on by default; the
 * V3DA_KNOB_* bits drop individual steps for A/B only.
 *
 * Two scheduling modes, switchable at runtime (DBG_SET_MODE, -m):
 *   SERIAL    one job on the hardware at a time, across all queues, oldest ready
 *             job first - exactly the old lane's order (bin N, render N, TFU, ...)
 *             with asynchronous completion. The bring-up default.
 *   PIPELINE  every queue runs one job concurrently: bin(N+1) overlaps render(N),
 *             TFU and CSD run beside CL work (Linux v3d_sched shape).
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

#include <sys/threads.h>

#include "v3da.h"
#include "v3da_regs.h"


#define WD_TICK_US        125000u     /* watchdog period per job (design section 7) */
#define WD_HARD_CAP_US    10000000u   /* even a progressing job is a runaway after 10 s */
#define OVF_POOL_BYTES    (32u * 1024u * 1024u)   /* old lane's BINOVF_PAGES 8192 */
#define VC_PROP_GET_VIRTUAL_WH     0x00040004u
#define VC_PROP_SET_VIRTUAL_OFFSET 0x00048009u

static const char *const qname[V3DA_Q_COUNT] = { "bin", "render", "tfu", "csd", "clean", "cpu" };
static uint64_t gseq;   /* global submission order (SERIAL picks the oldest ready job) */


static int is_hw_queue(int q)
{
	return ((q == V3DA_Q_BIN) || (q == V3DA_Q_RENDER) || (q == V3DA_Q_TFU) || (q == V3DA_Q_CSD)) ? 1 : 0;
}


int v3da_jobs_hw_busy(void)
{
	return (srv.nbusy != 0u) ? 1 : 0;
}


/* Concurrency accounting: called before every change of the busy set. */
static void acct(uint64_t now)
{
	uint64_t dt = (now > srv.acct_t_us) ? (now - srv.acct_t_us) : 0u;

	if (srv.nbusy >= 1u) {
		srv.any_busy_us += dt;
	}
	if (srv.nbusy >= 2u) {
		srv.overlap_us += dt;
	}
	srv.acct_t_us = now;
}


/* ========================================================================= */
/* Binner-overflow chunks (design section 4.3)                                */
/* ========================================================================= */

int v3da_ovf_init(void)
{
	v3da_ovf_t *o = &srv.ovf;
	uint32_t chunk = srv.ovf_chunk_kib * 1024u, i;
	int rc;

	if (chunk < 64u * 1024u) {
		chunk = 64u * 1024u;
	}
	if (chunk > OVF_POOL_BYTES) {
		chunk = OVF_POOL_BYTES;
	}
	chunk &= ~((uint32_t)_PAGE_SIZE - 1u);
	o->chunk_bytes = chunk;
	o->nchunks = OVF_POOL_BYTES / chunk;
	if (o->nchunks > V3DA_MAX_CHUNKS) {
		o->nchunks = V3DA_MAX_CHUNKS;
	}
	rc = v3da_bo_map_ovf_pool(o->nchunks * chunk, &o->gpuva);
	if (rc != 0) {
		o->nchunks = 0u;
		return rc;
	}
	o->free_head = -1;
	for (i = o->nchunks; i > 0u; i--) {
		o->next[i - 1u] = o->free_head;
		o->free_head = (int)(i - 1u);
	}
	o->nfree = o->nchunks;
	o->staged = -1;
	return 0;
}


static int chunk_alloc(void)
{
	v3da_ovf_t *o = &srv.ovf;
	int i = o->free_head;

	if (i >= 0) {
		o->free_head = o->next[i];
		o->next[i] = -1;
		o->nfree--;
	}
	return i;
}


static void chunk_free_list(int head)
{
	v3da_ovf_t *o = &srv.ovf;
	int next;

	while (head >= 0) {
		next = o->next[head];
		o->next[head] = o->free_head;
		o->free_head = head;
		o->nfree++;
		head = next;
	}
}


static void chunk_attach(v3da_job_t *j, int idx)
{
	srv.ovf.next[idx] = j->ovf_head;
	j->ovf_head = idx;
}


static uint32_t chunk_va(int idx)
{
	return srv.ovf.gpuva + (uint32_t)idx * srv.ovf.chunk_bytes;
}


/* Arm one free chunk for the IRQ handler / poll path (register writes only on
 * OUTOMEM, no thread wake-up in the binner's critical path). */
static void ovf_stage(void)
{
	v3da_ovf_t *o = &srv.ovf;

	if ((o->staged >= 0) || (o->nchunks == 0u)) {
		return;
	}
	o->staged = chunk_alloc();
	if (o->staged < 0) {
		return;
	}
	__atomic_store_n(&srv.hw.ovf_stage_size, o->chunk_bytes, __ATOMIC_RELEASE);
	__atomic_store_n(&srv.hw.ovf_stage_va, chunk_va(o->staged), __ATOMIC_RELEASE);
}


/* Grant one chunk directly (the binner is stalled on OUTOMEM with nothing staged). */
static int ovf_grant_direct(v3da_job_t *bin)
{
	int idx = chunk_alloc();

	if (idx < 0) {
		return -1;
	}
	srv.hw.core0[PTB_BPOA / 4u] = chunk_va(idx);
	srv.hw.core0[PTB_BPOS / 4u] = srv.ovf.chunk_bytes;
	chunk_attach(bin, idx);
	srv.q[V3DA_Q_BIN].st_oom++;
	return 0;
}


/* Event thread: attribute what the handler handed out, grant what it could not,
 * re-arm. Must run BEFORE FLDONE is processed (the chunk belongs to that job). */
static void ovf_service(void)
{
	v3da_ovf_t *o = &srv.ovf;
	v3da_job_t *bin = srv.q[V3DA_Q_BIN].active;
	uint32_t va, missed;
	int idx;

	va = __atomic_exchange_n(&srv.hw.ovf_consumed, 0u, __ATOMIC_ACQ_REL);
	if ((va != 0u) && (o->chunk_bytes != 0u)) {
		idx = (int)((va - o->gpuva) / o->chunk_bytes);
		if (idx == o->staged) {
			o->staged = -1;
		}
		if (bin != NULL) {
			chunk_attach(bin, idx);
			srv.q[V3DA_Q_BIN].st_oom++;
		}
		else {
			chunk_free_list(idx);   /* impossible by construction; do not leak it (next[idx] is -1) */
		}
	}
	missed = __atomic_exchange_n(&srv.hw.ovf_missed, 0u, __ATOMIC_ACQ_REL);
	if ((missed != 0u) && (bin != NULL)) {
		o->starved += missed;
		o->bin_waiting += (int)missed;
	}
	while ((o->bin_waiting > 0) && (bin != NULL) && (ovf_grant_direct(bin) == 0)) {
		o->bin_waiting--;
	}
	if (bin == NULL) {
		o->bin_waiting = 0;
	}
	ovf_stage();
}


/* ========================================================================= */
/* Completion                                                                 */
/* ========================================================================= */

static void job_done(v3da_job_t *j, int error, uint64_t now)
{
	int q = j->queue;
	v3da_queue_t *qu = &srv.q[q];
	uint64_t dt;

	if (qu->active == j) {
		if (is_hw_queue(q) != 0) {
			acct(now);
			srv.nbusy--;
		}
		qu->active = NULL;
		dt = (now > j->t_kick_us) ? (now - j->t_kick_us) : 0u;
		qu->st_busy_us += dt;
		if (dt > qu->st_max_us) {
			qu->st_max_us = (uint32_t)((dt > 0xffffffffu) ? 0xffffffffu : dt);
		}
	}
	else if (j->t_kick_us == 0u) {
		/* Completed without ever reaching the hardware (failed dependency): count
		 * it as submitted first, or hw_completed would run ahead of hw_submitted and
		 * let a quarantined BO pass while an older job still runs. */
		v3da_store64(&srv.fp->hdr.hw_submitted[q], v3da_load64(&srv.fp->hdr.hw_submitted[q]) + 1u);
	}
	qu->st_jobs++;
	if ((error != 0) || (j->error != 0)) {
		qu->st_errors++;
		error = 1;
	}

	if (q == V3DA_Q_BIN) {
		if (j->render != NULL) {
			j->render->bin = NULL;
			if (error != 0) {
				j->render->dep_error = 1;
			}
			/* The render reads the tile lists the binner wrote into these chunks. */
			while (j->ovf_head >= 0) {
				int idx = j->ovf_head;
				j->ovf_head = srv.ovf.next[idx];
				chunk_attach(j->render, idx);
			}
		}
		chunk_free_list(j->ovf_head);
		j->ovf_head = -1;
	}
	else if (j->ovf_head >= 0) {
		chunk_free_list(j->ovf_head);
		j->ovf_head = -1;
	}
	if (j->bin != NULL) {
		j->bin->render = NULL;
	}
	if (j->bos != NULL) {
		v3da_bo_unpin(j->bos, j->nbo);
		free(j->bos);
		j->bos = NULL;
	}
	v3da_fence_complete(j, error);
	srv.stat_jobs_seen++;
	free(j);
}


/* A discarded (never kicked) job of a dead client: no fence of its own (the
 * slot's dead_flush covers it), but its pins, chunks and CL link must go. */
static void job_discard(v3da_job_t *j)
{
	if (j->bin != NULL) {
		j->bin->render = NULL;
	}
	if (j->render != NULL) {
		j->render->bin = NULL;
	}
	if (j->ovf_head >= 0) {
		chunk_free_list(j->ovf_head);
	}
	if (j->bos != NULL) {
		v3da_bo_unpin(j->bos, j->nbo);
		free(j->bos);
	}
	free(j);
}


/* ========================================================================= */
/* Kick sequences (E2 steps)                                                  */
/* ========================================================================= */

static inline volatile uint32_t *C0(void)
{
	return srv.hw.core0;
}


static inline volatile uint32_t *HUB(void)
{
	return srv.hw.hub;
}


/* Step 5: MMU PTE-cache flush + TLB clear, every job unless the knob says
 * "only when a PTE changed since the last flush". */
static void tlb_step(void)
{
	if (((srv.knobs & V3DA_KNOB_TLB_ON_CHANGE) == 0u) || (srv.hw.tlb_gen != srv.hw.pt_gen)) {
		v3da_hw_mmu_flush(&srv.hw);
	}
}


/* A waited L2T flush: wait-old (GFXH-1897: never issue while one is pending),
 * issue, optionally wait-new. */
static void l2t_flush(int wait_new)
{
	v3da_hw_l2t_flush_wait(&srv.hw);
	C0()[CTL_L2TCACTL / 4u] = L2TCACTL_L2TFLS;
	if (wait_new != 0) {
		v3da_hw_l2t_flush_wait(&srv.hw);
	}
}


/* TMU write-combiner drain + waited L2T clean (Linux v3d_clean_caches; TFU/CSD
 * epilogues, SUBMIT_CL_FLUSH_CACHE). */
static void clean_caches(void)
{
	uint32_t spins;

	v3da_hw_l2t_flush_wait(&srv.hw);
	C0()[CTL_L2TCACTL / 4u] = L2TCACTL_TMUWCF;
	for (spins = 1000000u; (spins != 0u) && ((C0()[CTL_L2TCACTL / 4u] & L2TCACTL_TMUWCF) != 0u); spins--) {
	}
	C0()[CTL_L2TCACTL / 4u] = L2TCACTL_L2TFLS | L2TCACTL_FLM_CLEAN;
	v3da_hw_l2t_flush_wait(&srv.hw);
}


/* Stale-latch clear before a kick (E2 step 8's INT_CLR), made safe for overlap:
 * fold every pending status bit into the event words first (so another queue's
 * completion - render(N-1)'s FRDONE while bin(N) is kicked - is not lost), then
 * drop only THIS queue's own done bits, which cannot belong to anything else
 * because the queue is idle. OUTOMEM is left latched for the real path. */
static void drain_for_kick(uint32_t own_core, uint32_t own_hub)
{
	v3da_hw_drain(&srv.hw);
	if (own_core != 0u) {
		(void)__atomic_fetch_and(&srv.hw.ev_core, ~own_core, __ATOMIC_ACQ_REL);
	}
	if (own_hub != 0u) {
		(void)__atomic_fetch_and(&srv.hw.ev_hub, ~own_hub, __ATOMIC_ACQ_REL);
	}
}


static void kick_bin(v3da_job_t *j)
{
	const v3da_cl_desc_t *s = &j->d.cl;

	__asm__ volatile("dsb sy" ::: "memory");              /* steps 1, 3: Normal-NC BO stores -> DRAM */
	C0()[CTL_SLCACTL / 4u] = SLCACTL_INVAL_ALL;           /* step 4: early (#67 ordering fix) */
	tlb_step();                                           /* step 5 */
	l2t_flush(((srv.knobs & V3DA_KNOB_NO_L2T_WAIT_NEW) == 0u) ? 1 : 0);   /* step 6 */
	if ((srv.knobs & V3DA_KNOB_NO_FIXA) == 0u) {
		l2t_flush(1);                                     /* step 7: fix-A */
	}
	drain_for_kick(INT_FLDONE, 0u);                       /* step 8 */
	C0()[PTB_BPOS / 4u] = 0u;
	srv.ovf.bin_waiting = 0;
	ovf_stage();
	if (s->qma != 0u) {
		C0()[CLE_CT0QMA / 4u] = s->qma;
		C0()[CLE_CT0QMS / 4u] = s->qms;
	}
	if (s->qts != 0u) {
		C0()[CLE_CT0QTS / 4u] = CT0QTS_ENABLE | s->qts;
	}
	C0()[CLE_CT0QBA / 4u] = s->bcl_start;
	C0()[CLE_CT0QEA / 4u] = s->bcl_end;
}


static void kick_render(v3da_job_t *j)
{
	const v3da_cl_desc_t *s = &j->d.cl;

	/* Step 11, the bin->render hand-off, now the render prologue: waited L2T
	 * flush (binner tile lists to RAM before CT1 fetches them), then the slice
	 * invalidate. */
	l2t_flush(((srv.knobs & V3DA_KNOB_NO_HANDOFF_WAIT) == 0u) ? 1 : 0);
	C0()[CTL_SLCACTL / 4u] = SLCACTL_INVAL_ALL;
	drain_for_kick(INT_FRDONE, 0u);
	C0()[CLE_CT1QBA / 4u] = s->rcl_start;                 /* step 12 */
	C0()[CLE_CT1QEA / 4u] = s->rcl_end;
}


static void kick_tfu(v3da_job_t *j)
{
	const v3da_tfu_desc_t *t = &j->d.tfu;

	__asm__ volatile("dsb sy" ::: "memory");
	tlb_step();
	C0()[CTL_SLCACTL / 4u] = SLCACTL_INVAL_ALL;
	l2t_flush(1);
	drain_for_kick(0u, HUB_INT_TFUC | HUB_INT_TFUF);
	HUB()[TFU_IIA / 4u] = t->iia;
	HUB()[TFU_IIS / 4u] = t->iis;
	HUB()[TFU_ICA / 4u] = t->ica;
	HUB()[TFU_IUA / 4u] = t->iua;
	HUB()[TFU_IOA / 4u] = t->ioa;
	HUB()[TFU_IOS / 4u] = t->ios;
	HUB()[TFU_COEF0 / 4u] = t->coef[0];
	if ((t->coef[0] & TFU_COEF0_USECOEF) != 0u) {
		HUB()[TFU_COEF1 / 4u] = t->coef[1];
		HUB()[TFU_COEF2 / 4u] = t->coef[2];
		HUB()[TFU_COEF3 / 4u] = t->coef[3];
	}
	HUB()[TFU_ICFG / 4u] = t->icfg | TFU_ICFG_IOC;       /* this write starts the job */
}


static void wedge(const char *why, uint64_t now);


static int kick_csd(v3da_job_t *j)
{
	uint32_t spins, i;

	__asm__ volatile("dsb sy" ::: "memory");
	C0()[CTL_SLCACTL / 4u] = SLCACTL_INVAL_ALL;
	tlb_step();
	l2t_flush(1);
	/* Never kick into a unit that still has a CURRENT dispatch (winsys
	 * ioc_submit_csd: a queued kick behind stuck work was 3/8 boots of frozen
	 * vkQuake). One job per queue makes this rare; bounded, then reset. */
	for (spins = 8000000u; (spins != 0u) && ((C0()[CSD_STATUS / 4u] & CSD_STATUS_HAVE_CURRENT) != 0u); spins--) {
	}
	if (spins == 0u) {
		printf("V3DA srv CSD BUSY before kick (status=0x%08x) - resetting first\n", C0()[CSD_STATUS / 4u]);
		return -EBUSY;
	}
	drain_for_kick(INT_CSDDONE, 0u);
	for (i = 1u; i <= 6u; i++) {
		C0()[(CSD_QUEUED_CFG0 + 4u * i) / 4u] = j->d.csd.cfg[i];
	}
	C0()[CSD_QUEUED_CFG0 / 4u] = j->d.csd.cfg[0];         /* CFG0 starts the dispatch */
	return 0;
}


static void progress_sample(const v3da_job_t *j, uint32_t *ca, uint32_t *ra)
{
	switch (j->queue) {
		case V3DA_Q_BIN:
			*ca = C0()[CLE_CT0CA / 4u];
			*ra = C0()[CLE_CT0RA / 4u];
			break;
		case V3DA_Q_RENDER:
			*ca = C0()[CLE_CT1CA / 4u];
			*ra = C0()[CLE_CT1RA / 4u];
			break;
		case V3DA_Q_CSD:
			*ca = C0()[CSD_CURRENT_CFG4 / 4u];   /* batches left (Linux v3d_csd_job_timedout) */
			*ra = C0()[CSD_STATUS / 4u];
			break;
		default:
			*ca = HUB()[TFU_CS / 4u];
			*ra = 0u;
			break;
	}
}


static void job_kick(v3da_job_t *j, uint64_t now)
{
	int q = j->queue;
	v3da_queue_t *qu = &srv.q[q];

	qu->active = j;
	qu->pending--;
	j->t_kick_us = now;
	qu->st_wait_us += (now > j->t_submit_us) ? (now - j->t_submit_us) : 0u;
	v3da_store64(&srv.fp->hdr.hw_submitted[q], v3da_load64(&srv.fp->hdr.hw_submitted[q]) + 1u);
	if (is_hw_queue(q) != 0) {
		acct(now);
		srv.nbusy++;
		j->wd_check_us = now + WD_TICK_US;
		j->wd_progress_us = now;
	}

	switch (q) {
		case V3DA_Q_CPU:
			j->done_at_us = now + j->delay_us;
			break;
		case V3DA_Q_BIN:
			kick_bin(j);
			break;
		case V3DA_Q_RENDER:
			kick_render(j);
			break;
		case V3DA_Q_TFU:
			kick_tfu(j);
			break;
		case V3DA_Q_CSD:
			if (kick_csd(j) != 0) {
				j->error = 1;
				wedge("csd-busy-before-kick", now);   /* resets and fails this job */
				return;
			}
			break;
		default:
			break;
	}
	if (is_hw_queue(q) != 0) {
		progress_sample(j, &j->wd_ca, &j->wd_ra);
	}
}


/* ========================================================================= */
/* The scheduler                                                              */
/* ========================================================================= */

static void fifo_push(v3da_queue_t *q, uint32_t slot, v3da_job_t *j)
{
	j->next = NULL;
	if (q->tail[slot] != NULL) {
		q->tail[slot]->next = j;
	}
	else {
		q->head[slot] = j;
	}
	q->tail[slot] = j;
	q->pending++;
}


static v3da_job_t *fifo_pop(v3da_queue_t *q, uint32_t slot)
{
	v3da_job_t *j = q->head[slot];

	if (j != NULL) {
		q->head[slot] = j->next;
		if (q->head[slot] == NULL) {
			q->tail[slot] = NULL;
		}
		j->next = NULL;
	}
	return j;
}


/* 1 = may start now, 0 = not yet. Dependencies: the in-fences resolved at submit,
 * and (RENDER) its own bin job. */
static int job_ready(const v3da_job_t *j)
{
	uint32_t i;

	if ((j->queue == V3DA_Q_RENDER) && (j->bin != NULL)) {
		return 0;
	}
	for (i = 0u; i < j->ndep; i++) {
		if (v3da_fence_signaled(&j->dep[i], NULL) == 0) {
			return 0;
		}
	}
	return 1;
}


/* FIFO heads whose dependency failed complete (with an error) without running:
 * a render whose bin wedged must not be kicked against bad tile state (the old
 * lane skipped the render too). */
static void reap_failed_heads(uint64_t now)
{
	uint32_t s;
	int q, again = 1;
	v3da_job_t *j;

	while (again != 0) {
		again = 0;
		for (q = 0; q < V3DA_Q_COUNT; q++) {
			for (s = 0u; s < V3DA_MAX_CLIENTS; s++) {
				j = srv.q[q].head[s];
				if ((j != NULL) && (j->dep_error != 0) && (job_ready(j) != 0)) {
					(void)fifo_pop(&srv.q[q], s);
					srv.q[q].pending--;
					job_done(j, 1, now);
					again = 1;
				}
			}
		}
	}
}


void v3da_sched_run(void)
{
	uint64_t now = v3da_now_us();
	uint32_t i, idx, best_slot = 0u;
	int q, best_q = -1;
	v3da_queue_t *qu;
	v3da_job_t *j, *best = NULL;

	reap_failed_heads(now);

	/* CPU queue (NOP test jobs): always independent. */
	qu = &srv.q[V3DA_Q_CPU];
	if (qu->active == NULL) {
		for (i = 0u; i < V3DA_MAX_CLIENTS; i++) {
			idx = (qu->rr + i) % V3DA_MAX_CLIENTS;
			if ((qu->head[idx] != NULL) && (job_ready(qu->head[idx]) != 0)) {
				j = fifo_pop(qu, idx);
				qu->rr = idx + 1u;
				job_kick(j, now);
				break;
			}
		}
	}

	if (srv.mode == V3DA_MODE_SERIAL) {
		/* One hardware job at a time; the globally oldest ready one. */
		if (srv.nbusy != 0u) {
			return;
		}
		for (q = 0; q < V3DA_Q_COUNT; q++) {
			if (is_hw_queue(q) == 0) {
				continue;
			}
			for (i = 0u; i < V3DA_MAX_CLIENTS; i++) {
				j = srv.q[q].head[i];
				if ((j != NULL) && (job_ready(j) != 0) && ((best == NULL) || (j->gseq < best->gseq))) {
					best = j;
					best_q = q;
					best_slot = i;
				}
			}
		}
		if (best != NULL) {
			(void)fifo_pop(&srv.q[best_q], best_slot);
			job_kick(best, now);
		}
		return;
	}

	/* PIPELINE: every hardware queue independently, round-robin over clients. */
	for (q = 0; q < V3DA_Q_COUNT; q++) {
		qu = &srv.q[q];
		if ((is_hw_queue(q) == 0) || (qu->active != NULL)) {
			continue;
		}
		for (i = 0u; i < V3DA_MAX_CLIENTS; i++) {
			idx = (qu->rr + i) % V3DA_MAX_CLIENTS;
			if ((qu->head[idx] != NULL) && (job_ready(qu->head[idx]) != 0)) {
				j = fifo_pop(qu, idx);
				qu->rr = idx + 1u;
				job_kick(j, now);
				break;
			}
		}
	}
}


/* ========================================================================= */
/* Submission                                                                 */
/* ========================================================================= */

static v3da_job_t *job_new(v3da_client_t *c, int q, uint64_t now)
{
	v3da_job_t *j = calloc(1, sizeof(*j));

	if (j == NULL) {
		return NULL;
	}
	j->queue = q;
	j->client = c->id;
	j->ovf_head = -1;
	j->gseq = ++gseq;
	j->t_submit_us = now;
	j->fence.slot = (uint16_t)c->slot;
	j->fence.queue = (uint16_t)q;
	j->fence.gen = (uint32_t)srv.slot_gen[c->slot];
	return j;
}


/* Resolve an in-syncobj to a dependency AT SUBMIT TIME (DRM semantics). An empty
 * or signalled syncobj adds nothing (lenient: Mesa's first frame waits on a
 * freshly created out_sync). */
static void add_dep(v3da_job_t *j, const v3da_syncobj_t *s)
{
	if ((s->state == V3DA_SYNC_FENCE) && (v3da_fence_signaled(&s->fence, NULL) == 0) &&
			(j->ndep < V3DA_SUBMIT_MAX_SEMS)) {
		j->dep[j->ndep++] = s->fence;
	}
}


int v3da_submit(v3da_client_t *c, int op, const v3da_submit_t *hdr, const void *data, size_t size,
	v3da_submit_resp_t *out)
{
	const uint8_t *p = data;
	const uint32_t *bos;
	const v3da_sem_t *in, *outs;
	size_t need, want_desc;
	uint64_t now = v3da_now_us();
	v3da_job_t *first = NULL, *last = NULL;
	v3da_syncobj_t *s;
	uint32_t i;
	int rc;

	switch (op) {
		case V3DA_OP_SUBMIT_CL:  want_desc = sizeof(v3da_cl_desc_t); break;
		case V3DA_OP_SUBMIT_TFU: want_desc = sizeof(v3da_tfu_desc_t); break;
		case V3DA_OP_SUBMIT_CSD: want_desc = sizeof(v3da_csd_desc_t); break;
		default: return -EINVAL;
	}
	if ((hdr->desc_size != want_desc) || (hdr->nbo > V3DA_SUBMIT_MAX_BOS) ||
			(hdr->nin > V3DA_SUBMIT_MAX_SEMS) || (hdr->nout > V3DA_SUBMIT_MAX_SEMS)) {
		return -EINVAL;
	}
	need = want_desc + (size_t)hdr->nbo * sizeof(uint32_t) + ((size_t)hdr->nin + hdr->nout) * sizeof(v3da_sem_t);
	if ((p == NULL) || (size < need)) {
		return -EINVAL;
	}
	bos = (const uint32_t *)(p + want_desc);
	in = (const v3da_sem_t *)(p + want_desc + (size_t)hdr->nbo * sizeof(uint32_t));
	outs = in + hdr->nin;

	for (i = 0u; i < hdr->nin; i++) {
		if (v3da_syncobj_get(c, in[i].handle) == NULL) {
			return -ENOENT;
		}
	}
	for (i = 0u; i < hdr->nout; i++) {
		if (v3da_syncobj_get(c, outs[i].handle) == NULL) {
			return -ENOENT;
		}
	}
	if (op == V3DA_OP_SUBMIT_CL) {
		const v3da_cl_desc_t *d = (const v3da_cl_desc_t *)p;
		if ((d->bcl_end < d->bcl_start) || (d->rcl_end <= d->rcl_start)) {
			return -EINVAL;
		}
	}

	rc = v3da_bo_pin_for_job(bos, hdr->nbo);
	if (rc != 0) {
		return rc;
	}

	if (op == V3DA_OP_SUBMIT_CL) {
		first = job_new(c, V3DA_Q_BIN, now);
		last = job_new(c, V3DA_Q_RENDER, now);
		if ((first == NULL) || (last == NULL)) {
			free(first);
			free(last);
			v3da_bo_unpin(bos, hdr->nbo);
			return -ENOMEM;
		}
		memcpy(&first->d.cl, p, sizeof(first->d.cl));
		memcpy(&last->d.cl, p, sizeof(last->d.cl));
		first->render = last;
		last->bin = first;
	}
	else {
		first = job_new(c, (op == V3DA_OP_SUBMIT_TFU) ? V3DA_Q_TFU : V3DA_Q_CSD, now);
		if (first == NULL) {
			v3da_bo_unpin(bos, hdr->nbo);
			return -ENOMEM;
		}
		memcpy(&first->d, p, want_desc);
		last = first;
	}
	if (hdr->nbo != 0u) {
		last->bos = malloc((size_t)hdr->nbo * sizeof(uint32_t));
		if (last->bos == NULL) {
			if (first != last) {
				free(first);
			}
			free(last);
			v3da_bo_unpin(bos, hdr->nbo);
			return -ENOMEM;
		}
		memcpy(last->bos, bos, (size_t)hdr->nbo * sizeof(uint32_t));
		last->nbo = hdr->nbo;
	}

	/* In-syncs before out-syncs: gallium passes in_sync_rcl == out_sync. */
	for (i = 0u; i < hdr->nin; i++) {
		s = v3da_syncobj_get(c, in[i].handle);
		add_dep(((in[i].flags & V3DA_SEM_RENDER) != 0u) ? last : first, s);
	}

	first->fence.seqno = ++c->next_seq[first->queue];
	if (last != first) {
		last->fence.seqno = ++c->next_seq[last->queue];
	}
	v3da_bo_mark_use(bos, hdr->nbo, &last->fence);
	for (i = 0u; i < hdr->nout; i++) {
		s = v3da_syncobj_get(c, outs[i].handle);
		s->state = V3DA_SYNC_FENCE;
		s->fence = last->fence;
	}

	fifo_push(&srv.q[first->queue], c->slot, first);
	if (last != first) {
		fifo_push(&srv.q[last->queue], c->slot, last);
	}
	out->first = first->fence;
	out->last = last->fence;

	v3da_sched_run();
	v3da_kick_event_thread();
	return 0;
}


int v3da_submit_nop(v3da_client_t *c, uint32_t delay_us, v3da_fence_t *out)
{
	v3da_job_t *j;

	if (delay_us > 10000000u) {
		return -EINVAL;
	}
	j = job_new(c, V3DA_Q_CPU, v3da_now_us());
	if (j == NULL) {
		return -ENOMEM;
	}
	j->delay_us = delay_us;
	j->fence.seqno = ++c->next_seq[V3DA_Q_CPU];
	*out = j->fence;

	fifo_push(&srv.q[V3DA_Q_CPU], c->slot, j);
	v3da_sched_run();
	v3da_kick_event_thread();
	return 0;
}


uint32_t v3da_jobs_inflight(void)
{
	uint32_t n = 0u, s;
	int q;
	const v3da_job_t *j;

	for (q = 0; q < V3DA_Q_COUNT; q++) {
		if (srv.q[q].active != NULL) {
			n++;
		}
		for (s = 0u; s < V3DA_MAX_CLIENTS; s++) {
			for (j = srv.q[q].head[s]; j != NULL; j = j->next) {
				n++;
			}
		}
	}
	return n;
}


/* Discard a dead client's unstarted jobs. Their fences complete (with an error)
 * together with the client's in-flight job, or now if it has none. */
void v3da_jobs_client_gone(uint32_t client)
{
	uint32_t slot = client - 1u;
	int q;
	v3da_job_t *j;

	for (q = 0; q < V3DA_Q_COUNT; q++) {
		while ((j = fifo_pop(&srv.q[q], slot)) != NULL) {
			srv.q[q].pending--;
			job_discard(j);
		}
	}
	for (q = 0; q < V3DA_Q_COUNT; q++) {
		srv.dead_flush[slot][q] = srv.clients[slot].next_seq[q];
		if ((srv.q[q].active == NULL) || (srv.q[q].active->fence.slot != slot)) {
			if (srv.dead_flush[slot][q] > v3da_load64(&srv.fp->slot[slot].completed[q])) {
				v3da_store64(&srv.fp->slot[slot].error_seq,
					((uint64_t)q << 56) | (srv.dead_flush[slot][q] & 0x00ffffffffffffffULL));
				v3da_store64(&srv.fp->slot[slot].completed[q], srv.dead_flush[slot][q]);
			}
		}
	}
	v3da_sched_run();
}


/* ========================================================================= */
/* Completion events, watchdog, reset                                         */
/* ========================================================================= */

static void dump_job(const v3da_job_t *j)
{
	volatile uint32_t *c0 = C0();

	switch (j->queue) {
		case V3DA_Q_BIN:
			printf("V3DA srv BIN TIMEOUT int_sts=0x%08x ct0cs=0x%08x ct0ca=0x%08x[%x..%x] ct0ra=0x%08x gmp=0x%08x "
				"gmpvio=0x%08x mmu_ill=0x%08x ovf_free=%u waiting=%d\n",
				c0[CTL_INT_STS / 4u], c0[CLE_CT0CS / 4u], c0[CLE_CT0CA / 4u], j->d.cl.bcl_start, j->d.cl.bcl_end,
				c0[CLE_CT0RA / 4u], c0[GMP_STATUS / 4u], c0[GMP_VIO_ADDR / 4u], HUB()[MMU_ILLEGAL_ADDR / 4u],
				srv.ovf.nfree, srv.ovf.bin_waiting);
			printf("V3DA srv BIN PTB bpca=0x%08x bpcs=0x%08x bpoa=0x%08x bpos=0x%08x int_qpu=0x%03x\n",
				c0[PTB_BPCA / 4u], c0[PTB_BPCS / 4u], c0[PTB_BPOA / 4u], c0[PTB_BPOS / 4u],
				(c0[CTL_INT_STS / 4u] >> 16) & 0xfffu);
			break;
		case V3DA_Q_RENDER:
			printf("V3DA srv RENDER TIMEOUT int_sts=0x%08x ct1cs=0x%08x ct1ca=0x%08x[%x..%x] ct1ea=0x%08x gmp=0x%08x "
				"gmpvio=0x%08x mmu_ill=0x%08x qpu_int_acks=%u\n",
				c0[CTL_INT_STS / 4u], c0[CLE_CT1CS / 4u], c0[CLE_CT1CA / 4u], j->d.cl.rcl_start, j->d.cl.rcl_end,
				c0[CLE_CT1EA / 4u], c0[GMP_STATUS / 4u], c0[GMP_VIO_ADDR / 4u], HUB()[MMU_ILLEGAL_ADDR / 4u],
				j->qpu_acks);
			break;
		case V3DA_Q_TFU:
			printf("V3DA srv TFU TIMEOUT hub_int=0x%08x cs=0x%08x iia=0x%08x ioa=0x%08x ios=0x%08x icfg=0x%08x\n",
				HUB()[HUB_INT_STS / 4u], HUB()[TFU_CS / 4u], j->d.tfu.iia, j->d.tfu.ioa, j->d.tfu.ios, j->d.tfu.icfg);
			break;
		case V3DA_Q_CSD:
			printf("V3DA srv CSD TIMEOUT cfg0=0x%08x int_sts=0x%08x status=0x%08x cur_cfg4=0x%08x\n",
				j->d.csd.cfg[0], c0[CTL_INT_STS / 4u], c0[CSD_STATUS / 4u], c0[CSD_CURRENT_CFG4 / 4u]);
			break;
		default:
			break;
	}
	printf("V3DA srv DBG fdbgo=0x%08x fdbgb=0x%08x fdbgr=0x%08x fdbgs=0x%08x errstat=0x%08x\n",
		c0[ERR_FDBGO / 4u], c0[ERR_FDBGB / 4u], c0[ERR_FDBGR / 4u], c0[ERR_FDBGS / 4u], c0[ERR_STAT / 4u]);
}


/* Wedge (design section 7): dump, true reset, every job on the hardware
 * completes with an error (the reset killed all of them), queued jobs stay. The
 * wedge is data-dependent (re-submitting re-hangs, old lane), so a failed frame
 * is dropped, not retried. */
static void wedge(const char *why, uint64_t now)
{
	int q, rc, nfailed = 0;
	v3da_job_t *j;

	srv.wedges++;
	for (q = 0; q < V3DA_Q_COUNT; q++) {
		if ((is_hw_queue(q) != 0) && (srv.q[q].active != NULL)) {
			dump_job(srv.q[q].active);
		}
	}
	rc = v3da_hw_reset(&srv.hw);
	srv.resets++;
	srv.fp->hdr.reset_gen++;
	if (srv.ovf.staged >= 0) {
		chunk_free_list(srv.ovf.staged);
		srv.ovf.staged = -1;
	}
	srv.ovf.bin_waiting = 0;
	for (q = 0; q < V3DA_Q_COUNT; q++) {
		j = srv.q[q].active;
		if ((is_hw_queue(q) != 0) && (j != NULL)) {
			job_done(j, 1, now);
			nfailed++;
		}
	}
	printf("V3DA srv GPU wedged (%s) - true reset rc=%d, %d job(s) failed, drops=%u\n", why, rc, nfailed, srv.wedges);
}


/* Status bits drained from the handler (IRQ mode) or read by the poll path. */
void v3da_jobs_events(uint32_t core, uint32_t hub, uint64_t now)
{
	static unsigned mmu_logged;
	v3da_job_t *j;
	int failed;

	/* Overflow first: a chunk handed out during bin(N) belongs to bin(N) even if
	 * its FLDONE arrived in the same batch. */
	if (((core & INT_OUTOMEM) != 0u) || (srv.q[V3DA_Q_BIN].active != NULL)) {
		ovf_service();
	}

	if ((core & INT_FLDONE) != 0u) {
		j = srv.q[V3DA_Q_BIN].active;
		if (j != NULL) {
			job_done(j, 0, now);
		}
		else {
			srv.stray_fldone++;   /* the self-test's FLDONE, or a real anomaly */
		}
	}
	if ((core & INT_FRDONE) != 0u) {
		j = srv.q[V3DA_Q_RENDER].active;
		if (j != NULL) {
			/* Step 15: post-render clean, NOT waited (the next prologue's wait-old
			 * absorbs it). Step 16 only with the knob (old default: off). */
			v3da_hw_l2t_flush_wait(&srv.hw);
			C0()[CTL_L2TCACTL / 4u] = L2TCACTL_L2TFLS | L2TCACTL_FLM_CLEAN;
			if (((j->d.cl.flags & V3DA_CL_FLUSH_CACHE) != 0u) && ((srv.knobs & V3DA_KNOB_CL_CACHE_CLEAN) != 0u)) {
				clean_caches();
			}
			job_done(j, 0, now);
		}
	}
	if ((core & INT_CSDDONE) != 0u) {
		j = srv.q[V3DA_Q_CSD].active;
		if (j != NULL) {
			clean_caches();
			__asm__ volatile("dsb sy" ::: "memory");
			job_done(j, 0, now);
		}
	}
	if ((core & INT_GMPV) != 0u) {
		printf("V3DA srv GMP violation addr=0x%08x\n", C0()[GMP_VIO_ADDR / 4u]);
	}
	if ((hub & (HUB_INT_TFUC | HUB_INT_TFUF)) != 0u) {
		j = srv.q[V3DA_Q_TFU].active;
		if (j != NULL) {
			failed = ((hub & HUB_INT_TFUF) != 0u) ? 1 : 0;
			if (failed != 0) {
				printf("V3DA srv TFU FAIL hub_int=0x%08x cs=0x%08x iia=0x%08x ioa=0x%08x ios=0x%08x icfg=0x%08x\n",
					hub, HUB()[TFU_CS / 4u], j->d.tfu.iia, j->d.tfu.ioa, j->d.tfu.ios, j->d.tfu.icfg);
			}
			clean_caches();
			C0()[CTL_SLCACTL / 4u] = SLCACTL_INVAL_ALL;
			job_done(j, failed, now);
		}
		else if ((hub & HUB_INT_TFUC) != 0u) {
			srv.stray_tfuc++;
		}
	}
	if (((hub & HUB_INT_MMU_ANY) != 0u) && (mmu_logged < 16u)) {
		mmu_logged++;
		printf("V3DA srv MMU fault hub=0x%08x vio_id=0x%08x vio_addr=0x%08x mmu_ctl=0x%08x%s%s%s\n",
			hub, HUB()[MMU_VIO_ID / 4u], HUB()[MMU_VIO_ADDR / 4u], srv.hw.mmu_ctl_seen,
			((hub & HUB_INT_MMU_WRV) != 0u) ? " write-violation" : "",
			((hub & HUB_INT_MMU_PTI) != 0u) ? " pte-invalid" : "",
			((hub & HUB_INT_MMU_CAP) != 0u) ? " cap-exceeded" : "");
	}
}


/* Watchdog (design section 7), CPU jobs, overflow grants, flips, stats. */
static void present_run(void);
static void stat_print(uint64_t now);

void v3da_jobs_tick(uint64_t now)
{
	v3da_job_t *j;
	uint32_t ca, ra, sts;
	int q, stalled;

	j = srv.q[V3DA_Q_CPU].active;
	if ((j != NULL) && (now >= j->done_at_us)) {
		job_done(j, 0, now);
		srv.nops_done++;
	}

	for (q = 0; q < V3DA_Q_COUNT; q++) {
		j = srv.q[q].active;
		if ((is_hw_queue(q) == 0) || (j == NULL) || (now < j->wd_check_us)) {
			continue;
		}
		j->wd_check_us = now + WD_TICK_US;
		progress_sample(j, &ca, &ra);
		if ((ca != j->wd_ca) || (ra != j->wd_ra)) {
			j->wd_progress_us = now;
			j->wd_ca = ca;
			j->wd_ra = ra;
		}
		if (q == V3DA_Q_RENDER) {
			/* Step 13: ack latched QPU interrupt bits mid-render (only the QPU bits,
			 * never FRDONE). In poll mode the service routine already clears the
			 * raw status every poll; in IRQ mode the QPU sources are masked and
			 * would stay latched. */
			sts = C0()[CTL_INT_STS / 4u];
			if ((sts & INT_QPU_MASK) != 0u) {
				C0()[CTL_INT_CLR / 4u] = sts & INT_QPU_MASK;
				j->qpu_acks++;
			}
		}
		if ((q == V3DA_Q_BIN) && (srv.ovf.bin_waiting > 0)) {
			if (srv.q[V3DA_Q_RENDER].active != NULL) {
				j->wd_progress_us = now;   /* waiting for a render to free chunks: legitimate */
			}
			else if (srv.ovf.nfree == 0u) {
				printf("V3DA srv binner overflow pool EXHAUSTED (%u x %u KiB, none held by a running render)\n",
					srv.ovf.nchunks, srv.ovf.chunk_bytes / 1024u);
				j->error = 1;
				wedge("overflow-exhausted", now);
				continue;
			}
		}
		if ((q == V3DA_Q_TFU) && ((ca & TFU_CS_BUSY) == 0u) && ((now - j->t_kick_us) >= 2u * WD_TICK_US)) {
			/* The old lane's mask-independent fallback: BUSY cleared without a TFUC. */
			printf("V3DA srv TFU done without TFUC (cs=0x%08x) - completing\n", ca);
			clean_caches();
			C0()[CTL_SLCACTL / 4u] = SLCACTL_INVAL_ALL;
			job_done(j, 0, now);
			continue;
		}
		stalled = ((now - j->wd_progress_us) >= (uint64_t)srv.wedge_ms * 1000u) ? 1 : 0;
		if ((stalled != 0) || ((now - j->t_kick_us) >= WD_HARD_CAP_US)) {
			j->error = 1;
			wedge((stalled != 0) ? qname[q] : "runaway", now);
		}
	}

	if ((srv.ovf.bin_waiting > 0) && (srv.q[V3DA_Q_BIN].active != NULL)) {
		ovf_service();
	}
	present_run();
	stat_print(now);
}


/* Earliest CPU-job deadline (the event thread's timeout). */
uint64_t v3da_jobs_next_deadline(uint64_t now)
{
	const v3da_job_t *j = srv.q[V3DA_Q_CPU].active;

	if (j != NULL) {
		return (j->done_at_us > now) ? (j->done_at_us - now) : 1u;
	}
	return ~0ull;
}


/* ========================================================================= */
/* Modes, statistics                                                          */
/* ========================================================================= */

int v3da_set_mode(const v3da_mode_req_t *rq, v3da_mode_resp_t *out)
{
	/* Both switches are safe at any time: SERIAL only stops starting a second
	 * hardware job, PIPELINE only allows it. */
	if ((rq->set & V3DA_SET_MODE) != 0u) {
		if ((rq->mode != V3DA_MODE_SERIAL) && (rq->mode != V3DA_MODE_PIPELINE)) {
			return -EINVAL;
		}
		srv.mode = (int)rq->mode;
	}
	if ((rq->set & V3DA_SET_KNOBS) != 0u) {
		if ((rq->knobs & ~V3DA_KNOB_ALL) != 0u) {
			return -EINVAL;
		}
		srv.knobs = rq->knobs;
	}
	if (rq->set != 0u) {
		printf("V3DA srv mode=%s knobs=0x%02x\n", (srv.mode == V3DA_MODE_SERIAL) ? "serial" : "pipeline", srv.knobs);
		v3da_sched_run();
	}
	out->mode = (uint32_t)srv.mode;
	out->knobs = srv.knobs;
	out->rc = 0;
	return 0;
}


static void qstats_reset(uint64_t now)
{
	int q;

	for (q = 0; q < V3DA_Q_COUNT; q++) {
		srv.q[q].st_jobs = 0u;
		srv.q[q].st_errors = 0u;
		srv.q[q].st_max_us = 0u;
		srv.q[q].st_oom = 0u;
		srv.q[q].st_busy_us = 0u;
		srv.q[q].st_wait_us = 0u;
	}
	acct(now);
	srv.any_busy_us = 0u;
	srv.overlap_us = 0u;
	srv.acct_t0_us = now;
}


int v3da_qstats(const v3da_qstats_req_t *rq, v3da_resp_t *r)
{
	uint64_t now = v3da_now_us();
	const v3da_queue_t *qu;

	acct(now);
	if (rq->which == V3DA_QSTATS_GLOBAL) {
		v3da_qstats_g_t *g = &r->u.qstats_g;
		g->window_us = now - srv.acct_t0_us;
		g->any_busy_us = srv.any_busy_us;
		g->overlap_us = srv.overlap_us;
		g->mode = (uint32_t)srv.mode;
		g->knobs = srv.knobs;
		g->wedges = srv.wedges;
		g->resets = srv.resets;
		g->ovf_free = srv.ovf.nfree + ((srv.ovf.staged >= 0) ? 1u : 0u);   /* staged = free, just armed */
		g->ovf_total = srv.ovf.nchunks;
		g->ovf_starved = srv.ovf.starved;
		g->flips = srv.scan.flips;
	}
	else if (rq->which < V3DA_Q_COUNT) {
		v3da_qstats_q_t *o = &r->u.qstats_q;
		qu = &srv.q[rq->which];
		o->jobs = qu->st_jobs;
		o->errors = qu->st_errors;
		o->busy_us = qu->st_busy_us;
		o->wait_us = qu->st_wait_us;
		o->max_us = qu->st_max_us;
		o->oom = qu->st_oom;
		o->pending = qu->pending;
		o->active = (qu->active != NULL) ? 1u : 0u;
	}
	else {
		return -EINVAL;
	}
	if (rq->reset != 0u) {
		qstats_reset(now);
	}
	return 0;
}


/* One line per stat window while GPU jobs ran (cumulative counters, so a lost
 * line - ~1.3 % UART corruption - costs nothing but its own window). */
static void stat_print(uint64_t now)
{
	const v3da_queue_t *b = &srv.q[V3DA_Q_BIN], *r = &srv.q[V3DA_Q_RENDER];
	const v3da_queue_t *t = &srv.q[V3DA_Q_TFU], *c = &srv.q[V3DA_Q_CSD];

	if ((srv.stat_ms == 0u) || (now < srv.stat_next_us)) {
		return;
	}
	srv.stat_next_us = now + (uint64_t)srv.stat_ms * 1000u;
	if (srv.stat_jobs_seen == 0u) {
		return;
	}
	srv.stat_jobs_seen = 0u;
	acct(now);
	printf("V3DA srv qstat t=%llums mode=%s knobs=0x%02x bin=%u/%llums render=%u/%llums tfu=%u/%llums csd=%u/%llums "
		"busy=%llums overlap=%llums win=%llums oom=%u starved=%u err=%u wedges=%u flips=%u\n",
		(unsigned long long)(now / 1000u), (srv.mode == V3DA_MODE_SERIAL) ? "serial" : "pipeline", srv.knobs,
		b->st_jobs, (unsigned long long)(b->st_busy_us / 1000u), r->st_jobs, (unsigned long long)(r->st_busy_us / 1000u),
		t->st_jobs, (unsigned long long)(t->st_busy_us / 1000u), c->st_jobs, (unsigned long long)(c->st_busy_us / 1000u),
		(unsigned long long)(srv.any_busy_us / 1000u), (unsigned long long)(srv.overlap_us / 1000u),
		(unsigned long long)((now - srv.acct_t0_us) / 1000u), b->st_oom, srv.ovf.starved,
		b->st_errors + r->st_errors + t->st_errors + c->st_errors, srv.wedges, srv.scan.flips);
}


/* ========================================================================= */
/* Transitional present family: firmware pan through /dev/vcmbox              */
/* ========================================================================= */

int v3da_scanout_info(const v3da_scanout_req_t *rq, v3da_scanout_resp_t *out)
{
	uint32_t vw = 0u, vh = 0u, nbuf, i, claimed = 0u;
	int rc;

	if ((rq->pa == 0u) || (rq->pa > 0xffffffffull) || (rq->height == 0u) || (rq->pitch == 0u)) {
		return -EINVAL;
	}
	for (i = 0u; i < V3DA_SCANOUT_MAX; i++) {
		if (srv.scan.claimed[i] != 0u) {
			claimed |= 1u << i;
		}
	}
	if ((claimed != 0u) && ((srv.scan.pa != (uint32_t)rq->pa) || (srv.scan.height != rq->height) ||
			(srv.scan.pitch != rq->pitch))) {
		return -EBUSY;   /* a different framebuffer while scanout BOs still alias the old one */
	}
	/* GET_VIRTUAL_WH through the serialized mailbox: the granted virtual height
	 * decides how many stacked buffers exist (v3d_phoenix_fb_virtual_height). */
	rc = v3da_hw_vc_prop2(VC_PROP_GET_VIRTUAL_WH, 0u, 0u, 2u, &vw, &vh);
	if (rc != 0) {
		vh = 0u;
	}
	nbuf = (vh != 0u) ? (vh / rq->height) : 1u;
	if (nbuf > V3DA_SCANOUT_MAX) {
		nbuf = V3DA_SCANOUT_MAX;
	}
	if (nbuf < 1u) {
		nbuf = 1u;
	}
	srv.scan.pa = (uint32_t)rq->pa;
	srv.scan.width = rq->width;
	srv.scan.height = rq->height;
	srv.scan.pitch = rq->pitch;
	srv.scan.bytes = rq->pitch * rq->height;
	srv.scan.virt_h = vh;
	srv.scan.nbuf = nbuf;
	printf("V3DA srv scanout pa=0x%08x %ux%u pitch=%u virt=%ux%u rc=%d -> %u buffer(s)\n", srv.scan.pa, rq->width,
		rq->height, rq->pitch, vw, vh, rc, nbuf);
	out->nbuf = nbuf;
	out->virt_h = vh;
	out->bytes = srv.scan.bytes;
	out->claimed = claimed;
	return 0;
}


static void pan(uint32_t buf)
{
	(void)v3da_hw_vc_prop2(VC_PROP_SET_VIRTUAL_OFFSET, 0u, buf * srv.scan.height, 2u, NULL, NULL);
	srv.scan.shown = buf;
	srv.scan.flips++;
}


/* Pan every queued flip whose fence has passed, in order. */
static void present_run(void)
{
	uint32_t i;

	while ((srv.scan.nq != 0u) &&
			((srv.scan.q[0].gated == 0) || (v3da_fence_signaled(&srv.scan.q[0].fence, NULL) != 0))) {
		pan(srv.scan.q[0].buf);
		for (i = 1u; i < srv.scan.nq; i++) {
			srv.scan.q[i - 1u] = srv.scan.q[i];
		}
		srv.scan.nq--;
	}
}


int v3da_flip(v3da_client_t *c, const v3da_flip_req_t *rq, v3da_flip_resp_t *out)
{
	uint32_t buf = rq->buf;
	int gated = ((rq->flags & V3DA_FLIP_AFTER_FENCE) != 0u) ? 1 : 0;

	(void)c;
	if (srv.scan.nbuf == 0u) {
		return -ENODEV;
	}
	if (buf >= srv.scan.nbuf) {
		buf = srv.scan.nbuf - 1u;   /* the winsys clamps too */
	}
	if ((gated != 0) && (v3da_fence_valid(c, &rq->fence) == 0)) {
		return -EINVAL;
	}
	if ((srv.scan.nq == 0u) && ((gated == 0) || (v3da_fence_signaled(&rq->fence, NULL) != 0))) {
		pan(buf);
		out->deferred = 0u;
	}
	else {
		if (srv.scan.nq >= V3DA_MAX_FLIPS) {
			return -EBUSY;   /* the library then waits for the fence itself and flips ungated */
		}
		srv.scan.q[srv.scan.nq].buf = buf;
		srv.scan.q[srv.scan.nq].gated = gated;
		srv.scan.q[srv.scan.nq].fence = rq->fence;
		srv.scan.nq++;
		srv.scan.flips_deferred++;
		out->deferred = 1u;
		v3da_kick_event_thread();
	}
	out->pending = srv.scan.nq;
	out->flips = srv.scan.flips;
	return 0;
}
