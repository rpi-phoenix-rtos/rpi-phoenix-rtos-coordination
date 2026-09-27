/*
 * Phoenix-RTOS
 *
 * rpi4-v3d-async host test - the CSD profile (-C, v3da_csdprof.h), native gcc, no Pi
 *
 * Feeds the class table the CFG words Mesa v3dv builds for vkQuake's compute
 * dispatches (cmd_buffer_create_csd_job: CFG0-2 = count << 16, CFG3 = wg_size |
 * (wgs_per_sg << 8) | ((batches_per_sg - 1) << 12), CFG4 = batches - 1, CFG5 =
 * code address | THREADING | SINGLE_SEG | PROPAGATE_NANS) with scripted timestamps
 * at the BCM2711's 54 MHz generic timer, and checks the decode, the per-class
 * split, the time attribution (gpu / wake / pro / epi, the no-IRQ fallback), the
 * histogram edges and the rest slot once all classes are taken.
 *
 * -DCSDPROF_TEST_NEGCTL stamps INT_CSDDONE at the event thread instead of at the
 * interrupt (what a profile without the handler stamp would measure): the
 * wake/gpu attribution checks must then FAIL.
 *
 * Copyright 2026 Phoenix Systems
 * Author: Witold Bołt
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdio.h>
#include <string.h>

#include "v3da_csdprof.h"

#define HZ 54000000u
#define US(x) ((uint64_t)(x) * (HZ / 1000000u))

static int checks, fails;

#define CHECK(cond, ...) \
	do { \
		checks++; \
		if (!(cond)) { \
			fails++; \
			printf("CSDHOST FAIL %s:%d: ", __FILE__, __LINE__); \
			printf(__VA_ARGS__); \
			printf("\n"); \
		} \
	} while (0)


/* v3dv's packing, for a dispatch of x*y*z workgroups of wg_size invocations with
 * one workgroup per supergroup (what v3d_csd_choose_workgroups_per_supergroup
 * returns for a shader with a control barrier). */
static void v3dv_cfg(uint32_t cfg[7], uint32_t x, uint32_t y, uint32_t z, uint32_t wg_size, uint32_t code, int thr4)
{
	uint32_t bps = (wg_size + 15u) / 16u;

	memset(cfg, 0, 7u * sizeof(cfg[0]));
	cfg[0] = x << 16;
	cfg[1] = y << 16;
	cfg[2] = z << 16;
	cfg[3] = (1u << 8) | ((bps - 1u) << 12) | (wg_size & 0xffu);
	cfg[4] = bps * x * y * z - 1u;
	cfg[5] = code | (thr4 ? 1u : 0u) | 4u;
	cfg[6] = 0x00100000u;
}


static void job(v3da_csdprof_t *tab, const uint32_t cfg[7], uint64_t base, uint32_t pro_us, uint32_t gpu_us,
	uint32_t wake_us, uint32_t epi_us, int irq)
{
	v3da_csdprof_times_t t;

	t.t0 = base;
	t.t1 = t.t0 + US(pro_us);
#ifdef CSDPROF_TEST_NEGCTL
	t.done = irq ? t.t1 + US(gpu_us) + US(wake_us) : 0u;   /* stamp taken by the event thread: wake is lost */
#else
	t.done = irq ? t.t1 + US(gpu_us) : 0u;
#endif
	t.ev = t.t1 + US(gpu_us) + US(wake_us);
	t.end = t.ev + US(epi_us);
	v3da_csdprof_account(tab, cfg, &t, HZ);
}


int main(void)
{
	static v3da_csdprof_t tab[V3DA_CSDPROF_CLASSES];
	uint32_t warp[7], lm_small[7], lm_big[7], ind[7], other[7], i;
	v3da_csdprof_t *c;
	uint64_t base = 1000000u;

	/* cs_tex_warp: WARPIMAGESIZE/8 squared = 64x64 workgroups of 8x8 (gl_warp.c:130) */
	v3dv_cfg(warp, 64u, 64u, 1u, 64u, 0x00a00000u, 1);
	/* update_lightmap: w x h workgroups of 8x8 per dirty region (r_brush.c:3490) */
	v3dv_cfg(lm_small, 2u, 1u, 1u, 64u, 0x00a01000u, 0);
	v3dv_cfg(lm_big, 16u, 32u, 1u, 64u, 0x00a01000u, 0);
	/* indirect draw: (numsurfaces + 63) / 64 workgroups of 64 (r_brush.c:3541) */
	v3dv_cfg(ind, 40u, 1u, 1u, 64u, 0x00a02000u, 1);

	job(tab, warp, base, 20u, 2700u, 30u, 15u, 1);
	job(tab, warp, base + US(100000u), 20u, 2600u, 40u, 15u, 1);
	job(tab, lm_small, base + US(200000u), 20u, 240u, 30u, 15u, 1);
	job(tab, lm_big, base + US(300000u), 20u, 11000u, 30u, 15u, 1);
	job(tab, ind, base + US(400000u), 20u, 100u, 30u, 15u, 0);   /* completion seen only by the event thread */

	c = &tab[0];
	CHECK(c->cfg5 == warp[5], "class 0 key 0x%08x", c->cfg5);
	CHECK(c->n == 2u, "warp n=%u", c->n);
	CHECK((c->wg[0] == 64u) && (c->wg[1] == 64u) && (c->wg[2] == 1u), "warp wg %ux%ux%u", c->wg[0], c->wg[1], c->wg[2]);
	CHECK(c->wgs == 8192u && c->wgs_min == 4096u && c->wgs_max == 4096u, "warp wgs %llu %u..%u",
		(unsigned long long)c->wgs, c->wgs_min, c->wgs_max);
	CHECK((c->cfg3 & 0xffu) == 64u && ((c->cfg3 >> 8) & 0xfu) == 1u && ((c->cfg3 >> 12) & 0xffu) + 1u == 4u,
		"warp cfg3 0x%08x", c->cfg3);
	CHECK(c->batches == 2u * 16384u, "warp batches %llu", (unsigned long long)c->batches);
	CHECK(v3da_csdprof_us(c->gpu, HZ) == 5300u, "warp gpu_us %llu", (unsigned long long)v3da_csdprof_us(c->gpu, HZ));
	CHECK(v3da_csdprof_us(c->gpu_max, HZ) == 2700u, "warp max %llu", (unsigned long long)v3da_csdprof_us(c->gpu_max, HZ));
	CHECK(v3da_csdprof_us(c->wake, HZ) == 70u && v3da_csdprof_us(c->pro, HZ) == 40u && v3da_csdprof_us(c->epi, HZ) == 30u,
		"warp wake/pro/epi %llu/%llu/%llu", (unsigned long long)v3da_csdprof_us(c->wake, HZ),
		(unsigned long long)v3da_csdprof_us(c->pro, HZ), (unsigned long long)v3da_csdprof_us(c->epi, HZ));
	CHECK(c->hist[4] == 2u, "warp 2.6-2.7 ms in the 2000..4000 bucket (h4=%u)", c->hist[4]);
	CHECK((c->cfg5 & 1u) == 1u, "warp THREADING bit");

	c = &tab[1];
	CHECK(c->cfg5 == lm_small[5] && c->n == 2u, "lightmap one class for both sizes: key 0x%08x n=%u", c->cfg5, c->n);
	CHECK(c->wgs_min == 2u && c->wgs_max == 512u && c->wgs == 514u, "lightmap wgs %llu %u..%u", (unsigned long long)c->wgs,
		c->wgs_min, c->wgs_max);
	CHECK(c->hist[0] == 1u && c->hist[6] == 1u, "lightmap buckets (240 us, 11 ms) h0=%u h6=%u", c->hist[0], c->hist[6]);
	CHECK((c->cfg5 & 1u) == 0u, "lightmap single-threaded (no THREADING bit)");

	c = &tab[2];
	CHECK(c->cfg5 == ind[5] && c->n == 1u && c->no_irq == 1u, "indirect no_irq=%u", c->no_irq);
	CHECK(v3da_csdprof_us(c->gpu, HZ) == 130u && c->wake == 0u, "no-IRQ: gpu runs to the event thread (%llu us), no wake",
		(unsigned long long)v3da_csdprof_us(c->gpu, HZ));

	/* A stamp that does not fit between the CFG0 write and the event thread (a stale
	 * one, or a clock step) must not produce a negative or giant gpu time. */
	{
		v3da_csdprof_times_t t = { US(10u), US(20u), US(5u), US(120u), US(130u) };
		v3da_csdprof_account(tab, ind, &t, HZ);
		CHECK(tab[2].no_irq == 2u && v3da_csdprof_us(tab[2].gpu, HZ) == 230u && tab[2].wake == 0u,
			"stale stamp: no_irq=%u gpu=%llu (130 + 100)", tab[2].no_irq, (unsigned long long)v3da_csdprof_us(tab[2].gpu, HZ));
	}

	/* Histogram edges: 249 / 250 / 15999 / 16000 us. */
	{
		static v3da_csdprof_t e[V3DA_CSDPROF_CLASSES];
		job(e, warp, base, 1u, 249u, 1u, 1u, 1);
		job(e, warp, base, 1u, 250u, 1u, 1u, 1);
		job(e, warp, base, 1u, 15999u, 1u, 1u, 1);
		job(e, warp, base, 1u, 16000u, 1u, 1u, 1);
		CHECK(e[0].hist[0] == 1u && e[0].hist[1] == 1u && e[0].hist[6] == 1u && e[0].hist[7] == 1u,
			"edges h=%u/%u/%u/%u/%u/%u/%u/%u", e[0].hist[0], e[0].hist[1], e[0].hist[2], e[0].hist[3], e[0].hist[4],
			e[0].hist[5], e[0].hist[6], e[0].hist[7]);
	}

	/* More pipelines than classes: the last slot collects the rest and no class is
	 * overwritten. */
	for (i = 0u; i < 2u * V3DA_CSDPROF_CLASSES; i++) {
		v3dv_cfg(other, 1u, 1u, 1u, 16u, 0x00b00000u + 0x100u * i, 0);
		job(tab, other, base, 1u, 10u, 1u, 1u, 1);
	}
	CHECK(tab[0].cfg5 == warp[5] && tab[0].n == 2u, "warp class survived the flood");
	CHECK(tab[V3DA_CSDPROF_CLASSES - 1u].cfg5 == V3DA_CSDPROF_REST, "rest slot key 0x%08x", tab[V3DA_CSDPROF_CLASSES - 1u].cfg5);
	{
		uint32_t total = 0u;
		for (i = 0u; i < V3DA_CSDPROF_CLASSES; i++) {
			total += tab[i].n;
		}
		CHECK(total == 5u + 1u + 2u * V3DA_CSDPROF_CLASSES, "every job counted once: %u", total);
	}

	/* A CFG5 of 0 or of the rest key itself cannot claim a slot's "unused" or "rest" meaning. */
	{
		static v3da_csdprof_t z[V3DA_CSDPROF_CLASSES];
		uint32_t zc[7] = { 1u << 16, 1u << 16, 1u << 16, 16u, 0u, 0u, 0u };
		v3da_csdprof_times_t t = { 1u, 2u, 3u, 4u, 5u };
		v3da_csdprof_account(z, zc, &t, HZ);
		zc[5] = V3DA_CSDPROF_REST;
		v3da_csdprof_account(z, zc, &t, HZ);
		CHECK(z[0].cfg5 == 1u && z[0].n == 2u && z[1].cfg5 == 0u, "cfg5 0 / 0xffffffff map to key 1: %u n=%u",
			z[0].cfg5, z[0].n);
	}

	printf("CSDHOST RESULT checks=%d fails=%d verdict=%s\n", checks, fails, (fails == 0) ? "PASS" : "FAIL");
	return (fails == 0) ? 0 : 1;
}
