/*
 * rpi4-v3d-async host test - the BO block pool's byte cap and idle trim
 * (gpu/rpi4-v3d-async/v3da_pool.h, C17), native gcc, no Pi
 *
 * The server's block_put / block_get / v3da_bo_pool_tick glue is mirrored here
 * line for line around the real header; munmap is a ledger that checks every block
 * is released exactly once and never while it is still pooled.
 *
 * -DPOOL_TEST_NO_TRIM builds the same checks against the pre-C17 server (no cap,
 * no idle sweep: the pool only stops at V3DA_MAX_POOL blocks) - the negative
 * control: the bound checks must then FAIL.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <string.h>

#include "v3da_pool.h"

#define PG       4096u
#define MIB      (1024ull * 1024ull)
#define SEC      1000000ull
#define MAX_POOL 1024u          /* V3DA_MAX_POOL */
#define NBLK     4096u

static int checks, fails;

/* the mock kernel: a ledger of fake blocks */
static struct {
	int pooled[NBLK];
	int released[NBLK];
	int double_release, release_unpooled;
	uint32_t next;
} K;

/* the mirrored server state */
static struct {
	v3da_pool_block_t pool[MAX_POOL];
	uint32_t npool;
	uint64_t bytes, peak, cap, idle_us, scan_us;
	uint64_t trimmed;
	uint32_t to_kernel_full;
} S;


static void expect(const char *what, long long got, long long want)
{
	checks++;
	if (got != want) {
		fails++;
		printf("POOLHOST FAIL %s: got %lld want %lld\n", what, got, want);
	}
}


static void expect_le(const char *what, unsigned long long got, unsigned long long max)
{
	checks++;
	if (got > max) {
		fails++;
		printf("POOLHOST FAIL %s: %llu > %llu\n", what, got, max);
	}
}


static uint32_t id_of(const v3da_pool_block_t *b)
{
	return (uint32_t)((uintptr_t)b->cpu - 0x1000u) / 16u;
}


static __attribute__((unused)) void mock_release(void *ctx, const v3da_pool_block_t *b)
{
	uint32_t id = id_of(b);

	(void)ctx;
	if (K.released[id] != 0) {
		K.double_release++;
	}
	if (K.pooled[id] == 0) {
		K.release_unpooled++;
	}
	K.released[id] = 1;
	K.pooled[id] = 0;
}


static void reset(uint64_t cap, uint64_t idle_us)
{
	memset(&K, 0, sizeof(K));
	memset(&S, 0, sizeof(S));
	S.cap = cap;
	S.idle_us = idle_us;
}


static void srv_trim(uint64_t now, uint64_t idle_us)
{
#ifndef POOL_TEST_NO_TRIM
	v3da_pool_freed_t f;

	v3da_pool_trim(S.pool, &S.npool, &S.bytes, S.cap, now, idle_us, PG, mock_release, NULL, &f);
	S.trimmed += f.bytes;
#else
	(void)now;
	(void)idle_us;
#endif
}


/* v3da_bo.c block_put: a freed block (fresh id) enters the pool at `now` */
static uint32_t srv_put(uint32_t pages, uint64_t pa, uint64_t now)
{
	uint32_t id = K.next++;
	v3da_pool_block_t *pb;

	if (S.npool >= MAX_POOL) {
		S.to_kernel_full += pages;
		K.released[id] = 1;
		return id;
	}
	pb = &S.pool[S.npool++];
	pb->cpu = (void *)(uintptr_t)(0x1000u + id * 16u);
	pb->pa = (uintptr_t)pa;
	pb->pages = pages;
	pb->cached = 0u;
	pb->freed_us = now;
	K.pooled[id] = 1;
	S.bytes += v3da_pool_block_bytes(pb, PG);
	if (S.bytes > S.peak) {
		S.peak = S.bytes;
	}
	if (S.bytes > S.cap) {
		srv_trim(now, 0u);
	}
	return id;
}


/* v3da_bo.c block_get's pool path: 1 = reused a pooled block */
static int srv_get(uint32_t pages)
{
	int i = v3da_pool_pick(S.pool, S.npool, pages, 0u, 0, PG);

	if (i < 0) {
		return 0;
	}
	K.pooled[id_of(&S.pool[i])] = 0;
	S.bytes -= v3da_pool_block_bytes(&S.pool[i], PG);
	S.pool[i] = S.pool[--S.npool];
	return 1;
}


/* v3da_bo_pool_tick */
static void srv_tick(uint64_t now, int soon)
{
	if ((soon != 0) || (now >= S.scan_us)) {
		S.scan_us = now + SEC;
		srv_trim(now, S.idle_us);
	}
}


static uint64_t sum_bytes(void)
{
	uint64_t b = 0u;
	uint32_t i;

	for (i = 0u; i < S.npool; i++) {
		b += v3da_pool_block_bytes(&S.pool[i], PG);
	}
	return b;
}


#define HIGH(i) (0x80000000ull + (uint64_t)(i) * 0x01000000ull)   /* above 1 GiB */
#define LOW(i)  (0x10000000ull + (uint64_t)(i) * 0x01000000ull)   /* below 1 GiB */


int main(void)
{
	uint32_t i, first_kept;
	uint64_t t;

	/* footprint = the buddy block, the next power of two (what MEMMON sees) */
	{
		v3da_pool_block_t b = { .pages = 1261u };
		expect("footprint 1261 pages = 8 MiB", (long long)v3da_pool_block_bytes(&b, PG), (long long)(8u * MIB));
		b.pages = 769u;
		expect("footprint 769 pages = 4 MiB", (long long)v3da_pool_block_bytes(&b, PG), (long long)(4u * MIB));
		b.pages = 1u;
		expect("footprint 1 page = 4 KiB", (long long)v3da_pool_block_bytes(&b, PG), PG);
	}

	/* 1. the byte cap: twenty 4 MiB blocks into a 64 MiB pool keep the newest 16 */
	reset(64u * MIB, 10u * SEC);
	for (i = 0u; i < 20u; i++) {
		srv_put(769u, HIGH(i), (uint64_t)i * 1000u);
	}
	expect_le("cap: footprint <= 64 MiB", S.bytes, 64u * MIB);
	expect("cap: 16 blocks kept", S.npool, 16);
	expect("cap: oldest 4 released", K.released[0] + K.released[1] + K.released[2] + K.released[3], 4);
	expect("cap: newest kept", K.pooled[19], 1);
	expect("cap: accounting matches the blocks", (long long)S.bytes, (long long)sum_bytes());
	expect_le("cap: peak <= cap + one block", S.peak, 64u * MIB + 4u * MIB);

	/* 2. the idle sweep: nothing goes before the limit, everything after it */
	reset(64u * MIB, 10u * SEC);
	for (i = 0u; i < 4u; i++) {
		srv_put(769u, HIGH(i), 0u);
	}
	srv_tick(5u * SEC, 0);
	expect("idle: fresh blocks untouched at 5 s", S.npool, 4);
	srv_tick(10u * SEC, 0);
	expect("idle: empty at 10 s", S.npool, 0);
	expect("idle: footprint 0", (long long)S.bytes, 0);
	expect("idle: all released", K.released[0] + K.released[1] + K.released[2] + K.released[3], 4);

	/* 3. -T 0: no idle sweep, the cap alone */
	reset(64u * MIB, 0u);
	srv_put(769u, HIGH(0), 0u);
	srv_tick(3600u * SEC, 0);
	expect("-T 0: kept after an hour", S.npool, 1);

	/* 4. reuse keeps the accounting (block_get) */
	reset(64u * MIB, 10u * SEC);
	srv_put(1261u, HIGH(0), 0u);
	srv_put(769u, HIGH(1), 0u);
	expect("reuse: exact size picked", srv_get(1261u), 1);
	expect("reuse: accounting after get", (long long)S.bytes, (long long)(4u * MIB));
	expect("reuse: other size not picked", srv_get(1000u), 0);

	/* 5. over the cap, blocks above 1 GiB go before the (expensive) low ones */
	reset(8u * MIB, 0u);
	srv_put(769u, LOW(0), 0u);    /* oldest, low */
	srv_put(769u, HIGH(1), 10u);  /* high */
	srv_put(769u, HIGH(2), 20u);  /* newest, high: over the cap */
	expect("low pref: low block kept", K.pooled[0], 1);
	expect("low pref: oldest high released", K.released[1], 1);
	expect("low pref: newest high kept", K.pooled[2], 1);

	/* 6. -P 0: every freed block goes back at once */
	reset(0u, 10u * SEC);
	srv_put(1u, HIGH(0), 0u);
	expect("-P 0: nothing pooled", S.npool, 0);

	/* 7. the C17 session: a video page churning frame sizes (769 / 1261 / 2026 pages,
	 *    plus small BOs), then the browser exits and the system idles */
	reset(64u * MIB, 10u * SEC);
	t = 0u;
	for (i = 0u; i < 600u; i++) {
		static const uint32_t sizes[] = { 769u, 1261u, 2026u, 1u, 16u, 256u, 770u, 1262u };
		uint32_t pages = sizes[i % 8u] + (i / 8u) % 5u;   /* adaptive streams: many distinct sizes */

		t += 50000u;   /* a free every 50 ms */
		if ((i % 3u) == 0u) {
			(void)srv_get(pages);   /* some reuse */
		}
		srv_put(pages, HIGH(i % 64u), t);
		srv_tick(t, 0);
	}
	expect_le("session: footprint <= cap", S.bytes, 64u * MIB);
	expect_le("session: peak <= cap + one block", S.peak, 64u * MIB + 8u * MIB);
	expect("session: accounting matches the blocks", (long long)S.bytes, (long long)sum_bytes());
	srv_tick(t + 1u, 1);   /* the client goes (v3da_bo_client_gone) */
	expect_le("exit: at most the cap right away", S.bytes, 64u * MIB);
	for (t += SEC; t <= 700u * 50000u + 12u * SEC; t += 100000u) {
		srv_tick(t, 0);   /* the event thread, every 100 ms when idle */
	}
	expect("exit + 10 s idle: pool empty", S.npool, 0);
	expect("exit + 10 s idle: footprint 0", (long long)S.bytes, 0);
	first_kept = 0u;
	for (i = 0u; i < K.next; i++) {
		first_kept += (uint32_t)K.pooled[i];
	}
	expect("exit: no block left pooled in the ledger", first_kept, 0);

	expect("ledger: no double release", K.double_release, 0);
	expect("ledger: no release of a block not pooled", K.release_unpooled, 0);

	printf("POOLHOST RESULT checks=%d fails=%d verdict=%s\n", checks, fails, (fails == 0) ? "PASS" : "FAIL");
	return (fails == 0) ? 0 : 1;
}
