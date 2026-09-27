/*
 * Phoenix-RTOS
 *
 * Raspberry Pi 4 (BCM2711) V3D 4.2 asynchronous render server - CSD profile (-C)
 *
 * Compute (CSD) jobs aggregated per CFG5 value, i.e. per compute pipeline: CFG5 is
 * the shader's code address plus the THREADING / SINGLE_SEG / PROPAGATE_NANS bits
 * (Mesa v3dv_cmd_buffer.c, cmd_buffer_create_csd_job). Per class: the job count,
 * the workgroup and batch shape the client asked for (CFG0-2 counts, CFG3 wg_size /
 * wgs_per_sg / batches_per_sg - 1, CFG4 batches - 1 on V3D < 7.1.6), and where a
 * job's time goes, in generic-timer (CNTVCT) ticks:
 *   pro   kick prologue: dsb, slice invalidate, TLB flush, waited L2T flush,
 *         CFG1..6 + CFG0 writes
 *   gpu   the CFG0 write to the first sight of INT_CSDDONE (IRQ handler, poll path
 *         or a pre-kick drain) - the dispatch executing
 *   wake  INT_CSDDONE to the event thread taking the completion
 *   epi   the completion's TMU write-combiner drain + L2T clean
 * plus a histogram of the gpu time. Written for docs/gpu-new-lane/vkquake-perf.md
 * ("CSD per-job cost"): the qstat line gives only the CSD total.
 *
 * Pure functions over a class table (no Phoenix headers, no server state), so
 * tools/gpu-lane/v3d-async/hosttest/run.sh checks exactly what the server counts.
 *
 * Copyright 2026 Phoenix Systems
 * Author: Witold Bołt
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef V3DA_CSDPROF_H
#define V3DA_CSDPROF_H

#include <stdint.h>

#define V3DA_CSDPROF_CLASSES 16u      /* the last slot collects every further CFG5 (cfg5 = 0xffffffff) */
#define V3DA_CSDPROF_BUCKETS 8u       /* gpu time: <250 <500 <1000 <2000 <4000 <8000 <16000 >=16000 us */
#define V3DA_CSDPROF_REST    0xffffffffu

typedef struct {
	uint32_t cfg5;                /* key; 0 = unused slot */
	uint32_t cfg3;                /* last job's CFG3 */
	uint32_t wg[3];               /* last job's workgroup counts */
	uint32_t wgs_min, wgs_max;    /* workgroups per job */
	uint32_t n, n_printed;
	uint32_t no_irq;              /* completions without a CSDDONE stamp (gpu = up to the event thread) */
	uint64_t wgs, batches;        /* sums */
	uint64_t pro, gpu, wake, epi; /* sums, ticks */
	uint64_t gpu_max;
	uint32_t hist[V3DA_CSDPROF_BUCKETS];
} v3da_csdprof_t;

/* One job's timestamps (CNTVCT ticks). */
typedef struct {
	uint64_t t0;                  /* kick prologue start */
	uint64_t t1;                  /* CFG0 written */
	uint64_t done;                /* INT_CSDDONE first seen, 0 = never */
	uint64_t ev;                  /* the event thread took the completion */
	uint64_t end;                 /* the completion's cache clean returned */
} v3da_csdprof_times_t;


static inline uint64_t v3da_csdprof_us(uint64_t ticks, uint64_t cntfrq)
{
	return (cntfrq != 0u) ? (ticks * 1000000u) / cntfrq : 0u;
}


/* The class a CFG5 value counts in: its own, a free one, or the rest slot. */
static inline v3da_csdprof_t *v3da_csdprof_class(v3da_csdprof_t *tab, uint32_t cfg5)
{
	uint32_t key = ((cfg5 != 0u) && (cfg5 != V3DA_CSDPROF_REST)) ? cfg5 : 1u, i;

	for (i = 0u; i < V3DA_CSDPROF_CLASSES - 1u; i++) {
		if ((tab[i].cfg5 == key) || (tab[i].cfg5 == 0u)) {
			tab[i].cfg5 = key;
			return &tab[i];
		}
	}
	tab[i].cfg5 = V3DA_CSDPROF_REST;
	return &tab[i];
}


static inline void v3da_csdprof_account(v3da_csdprof_t *tab, const uint32_t cfg[7], const v3da_csdprof_times_t *t,
	uint64_t cntfrq)
{
	static const uint32_t edge_us[V3DA_CSDPROF_BUCKETS - 1u] = { 250u, 500u, 1000u, 2000u, 4000u, 8000u, 16000u };
	v3da_csdprof_t *c = v3da_csdprof_class(tab, cfg[5]);
	uint64_t gpu, wake, us, wgs;
	uint32_t b;

	c->cfg3 = cfg[3];
	c->wg[0] = cfg[0] >> 16;
	c->wg[1] = cfg[1] >> 16;
	c->wg[2] = cfg[2] >> 16;
	wgs = (uint64_t)c->wg[0] * c->wg[1] * c->wg[2];
	if (wgs > 0xffffffffu) {
		wgs = 0xffffffffu;
	}
	if ((c->n == 0u) || (wgs < c->wgs_min)) {
		c->wgs_min = (uint32_t)wgs;
	}
	if (wgs > c->wgs_max) {
		c->wgs_max = (uint32_t)wgs;
	}
	c->wgs += wgs;
	c->batches += (uint64_t)cfg[4] + 1u;

	if ((t->done != 0u) && (t->done >= t->t1) && (t->done <= t->ev)) {
		gpu = t->done - t->t1;
		wake = t->ev - t->done;
	}
	else {
		gpu = (t->ev > t->t1) ? (t->ev - t->t1) : 0u;
		wake = 0u;
		c->no_irq++;
	}
	c->pro += (t->t1 > t->t0) ? (t->t1 - t->t0) : 0u;
	c->gpu += gpu;
	c->wake += wake;
	c->epi += (t->end > t->ev) ? (t->end - t->ev) : 0u;
	if (gpu > c->gpu_max) {
		c->gpu_max = gpu;
	}
	us = v3da_csdprof_us(gpu, cntfrq);
	for (b = 0u; (b < V3DA_CSDPROF_BUCKETS - 1u) && (us >= edge_us[b]); b++) {
	}
	c->hist[b]++;
	c->n++;
}

#endif
