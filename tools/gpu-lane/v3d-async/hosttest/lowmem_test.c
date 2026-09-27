/*
 * Phoenix-RTOS
 *
 * rpi4-v3d-async host test - scan-out placement below 1 GiB (V3DA_BO_LOWMEM,
 * v3da_lowmem.h), native gcc, no Pi
 *
 * The allocator is a mock that hands out a scripted sequence of blocks (low, high,
 * torn = not contiguous, none) and checks what the policy does with them: which
 * block it keeps, that every other block is handed back exactly once, and that the
 * rejects are HELD while it tries (a reject released before the next try would be
 * handed out again by the kernel's buddy allocator).
 *
 * -DLOWMEM_TEST_NO_POLICY builds the same checks against "take the first block"
 * (what the server did before proto 5) - the negative control: the placement
 * checks must then FAIL.
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

#include "v3da_lowmem.h"

#define PG    4096u
#define MIB   (1024u * 1024u)
#define P1080 2026u   /* a 1920x1080 XRGB buffer + Mesa's TFU read-ahead padding, as on the Pi */

enum { B_LOW = 1, B_HIGH, B_TORN, B_NONE, B_UNRESOLVED };

static int checks, fails;

static struct {
	int script[32];
	uint32_t nscript, next;
	uint64_t pa[32];
	int mapped[32], unmapped[32];
	uint32_t live, live_max;
	char cpu[32];
} M;


static void *mock_map(void *ctx, size_t bytes, int cached, uint64_t *first, uint64_t *last)
{
	uint32_t i = M.next;
	int kind;

	(void)ctx;
	(void)cached;
	if (i >= M.nscript) {
		return NULL;
	}
	kind = M.script[i];
	M.next++;
	if (kind == B_NONE) {
		return NULL;
	}
	/* 8 MiB-aligned buddy blocks (a 2026-page block is one 8 MiB buddy block): low ones
	 * from 0x20000000 up, high ones from 0xf0000000 */
	M.pa[i] = ((kind == B_HIGH) ? 0xf0000000ull : 0x20000000ull) + (uint64_t)i * 8u * MIB;
	*first = M.pa[i];
	*last = M.pa[i] + bytes - PG + ((kind == B_TORN) ? 0x100000ull : 0u);
	if (kind == B_UNRESOLVED) {
		*first = *last = ~0ull;   /* va2pa failed: (addr_t)-1 for both pages */
	}
	M.mapped[i] = 1;
	M.live++;
	if (M.live > M.live_max) {
		M.live_max = M.live;
	}
	return &M.cpu[i];
}


static void mock_unmap(void *ctx, void *cpu, size_t bytes)
{
	uint32_t i = (uint32_t)((char *)cpu - M.cpu);

	(void)ctx;
	(void)bytes;
	M.unmapped[i]++;
	M.live--;
}


static const v3da_lowmem_ops_t ops = { mock_map, mock_unmap, NULL };


#ifdef LOWMEM_TEST_NO_POLICY
/* Before proto 5: the first block the kernel hands out, wherever it lies; the first
 * pooled block of the size. */
static void *place(const v3da_lowmem_ops_t *o, size_t bytes, int cached, uint32_t tries, uint32_t pgsz, uint64_t *pa,
	v3da_lowmem_result_t *res)
{
	uint64_t first = 0, last = 0;
	void *cpu = o->map(o->ctx, bytes, cached, &first, &last);

	(void)tries;
	(void)pgsz;
	memset(res, 0, sizeof(*res));
	if (cpu != NULL) {
		res->tries = 1;
		res->low = v3da_lowmem_is_low(first, bytes);
		*pa = first;
	}
	return cpu;
}

static int pick(const v3da_pool_block_t *pool, uint32_t n, uint32_t pages, uint32_t cached, int want_low, uint32_t pgsz)
{
	uint32_t i;

	(void)want_low;
	(void)pgsz;
	for (i = 0; i < n; i++) {
		if ((pool[i].pages == pages) && (pool[i].cached == cached)) {
			return (int)i;
		}
	}
	return -1;
}
#else
#define place v3da_lowmem_map
#define pick  v3da_pool_pick
#endif


static void expect(const char *what, long long got, long long want)
{
	checks++;
	if (got != want) {
		fails++;
	}
	printf("LOWHOST %-52s got=%-10lld want=%-10lld %s\n", what, got, want, (got == want) ? "ok" : "FAIL");
}


/* Run the placement over a script; returns the index of the kept block or -1. */
static int run_pages(const char *name, const int *script, uint32_t n, uint32_t tries, uint32_t pages,
	v3da_lowmem_result_t *res, int *leak)
{
	uint64_t pa = 0;
	void *cpu;
	uint32_t i;
	int kept;

	memset(&M, 0, sizeof(M));
	memcpy(M.script, script, n * sizeof(*script));
	M.nscript = n;
	cpu = place(&ops, (size_t)pages * PG, 0, tries, PG, &pa, res);
	kept = (cpu != NULL) ? (int)((char *)cpu - M.cpu) : -1;
	*leak = 0;
	for (i = 0; i < M.next; i++) {
		if (!M.mapped[i]) {
			continue;
		}
		/* every block mapped is either the kept one (never unmapped) or unmapped once */
		if (((int)i == kept) ? (M.unmapped[i] != 0) : (M.unmapped[i] != 1)) {
			(*leak)++;
		}
	}
	if ((cpu != NULL) && (pa != M.pa[kept])) {
		(*leak)++;   /* the reported address is not the kept block's */
	}
	printf("LOWHOST case %-18s tries=%u rejected=%u low=%d kept=%d held_max=%u\n", name, res->tries, res->rejected, res->low,
		kept, M.live_max);
	return kept;
}


static int run(const char *name, const int *script, uint32_t n, uint32_t tries, v3da_lowmem_result_t *res, int *leak)
{
	return run_pages(name, script, n, tries, P1080, res, leak);
}


int main(void)
{
	v3da_lowmem_result_t r;
	int leak, k;

	/* footprint: vm_objectContiguous takes one buddy block (the next power of two) */
	expect("footprint 1 page", (long long)v3da_lowmem_footprint(1, PG), PG);
	expect("footprint 2048 pages (8 MiB exactly)", (long long)v3da_lowmem_footprint(2048, PG), 8ll * MIB);
	expect("footprint 2026 pages (1080p + padding) = 8 MiB", (long long)v3da_lowmem_footprint(P1080, PG), 8ll * MIB);
	expect("footprint 2049 pages", (long long)v3da_lowmem_footprint(2049, PG), 16ll * MIB);

	/* the limit, to the byte */
	expect("is_low block ending exactly at 1 GiB", v3da_lowmem_is_low(0x3f800000ull, 8ull * MIB), 1);
	expect("is_low block crossing 1 GiB", v3da_lowmem_is_low(0x3fc00000ull, 8ull * MIB), 0);
	expect("is_low m6h-g7's refused buffer (pa 0xf8000000)", v3da_lowmem_is_low(0xf8000000ull, (uint64_t)P1080 * PG), 0);
	expect("is_low wrap", v3da_lowmem_is_low(~0ull - PG, 2ull * PG), 0);
	expect("is_low zero size", v3da_lowmem_is_low(0x1000ull, 0), 0);

	/* the budget */
	expect("admit 8th 8 MiB block in 64 MiB (the default)", v3da_lowmem_admit(56ull * MIB, 8ull * MIB, 64ull * MIB), 1);
	expect("admit 9th 8 MiB block in 64 MiB", v3da_lowmem_admit(64ull * MIB, 8ull * MIB, 64ull * MIB), 0);
	expect("admit with budget 0 (-L 0)", v3da_lowmem_admit(0, 8ull * MIB, 0), 0);
	expect("admit overflow-safe", v3da_lowmem_admit(~0ull, 8ull * MIB, ~0ull), 0);

	/* the pool: exact size and memory type; LOWMEM only low; ordinary prefers high */
	{
		v3da_pool_block_t pool[5] = {
			{ NULL, 0xf8000000u, P1080, 0 },   /* 0: high, uncached */
			{ NULL, 0x29000000u, P1080, 0 },   /* 1: low, uncached */
			{ NULL, 0x2a000000u, P1080, 1 },   /* 2: low, cached */
			{ NULL, 0x2b000000u, 1000, 0 },    /* 3: low, other size */
			{ NULL, 0x2c000000u, P1080, 0 },   /* 4: low, uncached */
		};
		expect("pool LOWMEM takes the low block", pick(pool, 5, P1080, 0, 1, PG), 1);
		expect("pool ordinary takes the high block first", pick(pool, 5, P1080, 0, 0, PG), 0);
		expect("pool ordinary falls back to a low block", pick(pool + 1, 4, P1080, 0, 0, PG), 0);
		expect("pool LOWMEM never takes a high block", pick(pool, 1, P1080, 0, 1, PG), -1);
		expect("pool memory type must match", pick(pool, 5, P1080, 1, 1, PG), 2);
		expect("pool size must match", pick(pool, 5, 999, 0, 0, PG), -1);
	}

	/* fresh blocks */
	{
		static const int s1[] = { B_LOW };
		k = run("low-first", s1, 1, V3DA_LOWMEM_TRIES, &r, &leak);
		expect("low-first: kept", k, 0);
		expect("low-first: low", r.low, 1);
		expect("low-first: tries", r.tries, 1);
		expect("low-first: no leak", leak, 0);
	}
	{
		static const int s2[] = { B_HIGH, B_HIGH, B_LOW, B_LOW };
		k = run("high-high-low", s2, 4, V3DA_LOWMEM_TRIES, &r, &leak);
		expect("high-high-low: kept the low one", k, 2);
		expect("high-high-low: low", r.low, 1);
		expect("high-high-low: tries", r.tries, 3);
		expect("high-high-low: rejects handed back", r.rejected, 2);
		expect("high-high-low: rejects held while trying", M.live_max, 3);
		expect("high-high-low: no leak", leak, 0);
	}
	{
		static const int s3[] = { B_HIGH, B_HIGH, B_HIGH, B_HIGH, B_HIGH, B_HIGH, B_HIGH, B_HIGH, B_HIGH, B_HIGH, B_HIGH,
			B_HIGH, B_HIGH, B_HIGH, B_HIGH, B_HIGH, B_LOW, B_LOW };
		k = run("all-high", s3, 18, V3DA_LOWMEM_TRIES, &r, &leak);
		expect("all-high: stops after 16 tries", r.tries, 16);
		expect("all-high: keeps the last (fallback)", k, 15);
		expect("all-high: not low", r.low, 0);
		expect("all-high: 15 handed back", r.rejected, 15);
		expect("all-high: no leak", leak, 0);
	}
	{
		static const int s4[] = { B_TORN, B_LOW };
		k = run("torn-low", s4, 2, V3DA_LOWMEM_TRIES, &r, &leak);
		expect("torn-low: a torn low block is never kept", k, 1);
		expect("torn-low: no leak", leak, 0);
	}
	{
		static const int s5[] = { B_HIGH, B_NONE };
		k = run("high-then-none", s5, 2, V3DA_LOWMEM_TRIES, &r, &leak);
		expect("high-then-none: keeps the high block", k, 0);
		expect("high-then-none: not low", r.low, 0);
		expect("high-then-none: no leak", leak, 0);
	}
	{
		static const int s6[] = { B_TORN, B_NONE };
		k = run("torn-then-none", s6, 2, V3DA_LOWMEM_TRIES, &r, &leak);
		expect("torn-then-none: nothing usable", k, -1);
		expect("torn-then-none: no leak", leak, 0);
	}
	{
		/* one page: first == last, so only the explicit (addr_t)-1 check refuses it */
		static const int s8[] = { B_UNRESOLVED, B_NONE };
		k = run_pages("unresolved-1page", s8, 2, V3DA_LOWMEM_TRIES, 1, &r, &leak);
		expect("unresolved-1page: a block va2pa cannot resolve is never kept", k, -1);
		expect("unresolved-1page: no leak", leak, 0);
	}
	{
		static const int s7[] = { B_NONE };
		k = run("none", s7, 1, V3DA_LOWMEM_TRIES, &r, &leak);
		expect("none: NULL", k, -1);
		expect("none: tries", r.tries, 0);
	}

	printf("LOWHOST RESULT checks=%d fails=%d verdict=%s\n", checks, fails, (fails == 0) ? "PASS" : "FAIL");
	return (fails == 0) ? 0 : 1;
}
