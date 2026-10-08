/*
 * batchtest - host test for the /dev/wifibatch frame format (wifibatch.h)
 *
 * Compiles the daemon's own wifi/rpi4-wifi/wifibatch.h (no copy); the Makefile
 * first checks that the netif's drivers/wifibatch.h is byte-identical to it.
 *
 *   1. round trip: random frames packed by the writer come back from the reader
 *      byte for byte, in order, with the flags; a slot is refused exactly when
 *      the record would not fit;
 *   2. the TX credit protocol: a model daemon takes frames from the front of a
 *      batch until a random credit window shuts, and the netif's flush loop
 *      (wifibatch_drop + resend, as wifi_txFlush) must deliver every frame
 *      exactly once, in order;
 *   3. the RX fill: a model daemon fills a reader-sized buffer the way
 *      wifi_batchRead does (a slot for a full F2 frame before each frame);
 *   4. malformed input: every truncation of a good batch, a corrupted length,
 *      a non-zero reserved field, a zero count and trailing bytes are refused
 *      by wifibatch_open, and never read out of bounds (run it under ASan).
 *
 * A checker that cannot fail proves nothing: the TX model is first run with a
 * flush loop that forgets the wifibatch_drop (resends the whole batch), and the
 * checker must report the duplicates.
 *
 * Copyright 2026 Phoenix Systems
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wifibatch.h"

#define F2_FRAME_MAX   2048u /* rpi4-wifi.c */
#define TX_MIN         14u   /* wifi43455.c WIFI_TX_MIN */
#define TX_MAX         2032u /* wifi43455.c WIFI_TX_MAX */
#define TX_BUF         (24u * 1024u)
#define TX_FRAMES      16u
#define RX_BUF         (24u * 1024u)
#define RX_FRAMES      32u

static unsigned long g_fail;
static uint32_t g_seed = 12345u;

#define CHECK(c, ...) \
	do { \
		if (!(c)) { \
			g_fail++; \
			if (g_fail <= 20u) { \
				printf("FAIL %s:%d: ", __FILE__, __LINE__); \
				printf(__VA_ARGS__); \
				printf("\n"); \
			} \
		} \
	} while (0)


static uint32_t rnd(void)
{
	g_seed = g_seed * 1103515245u + 12345u;
	return (g_seed >> 8) & 0xffffffu;
}


/* A frame's content is a function of its sequence number, so the receiver can
 * check it from the bytes alone. */
static uint32_t frameLen(uint32_t seq)
{
	switch (seq % 7u) {
		case 0: return TX_MIN;
		case 1: return 1514u;
		case 2: return TX_MAX;
		case 3: return 54u + (seq % 4u); /* a TCP ACK-sized frame, odd lengths */
		default: return TX_MIN + ((seq * 2654435761u) % (TX_MAX - TX_MIN + 1u));
	}
}


static void frameFill(uint8_t *f, uint32_t seq, uint32_t len)
{
	uint32_t i;

	for (i = 0; i < len; ++i) {
		f[i] = (uint8_t)((seq * 31u) + (i * 7u) + (i >> 8));
	}
	if (len >= 4u) {
		f[0] = (uint8_t)seq;
		f[1] = (uint8_t)(seq >> 8);
		f[2] = (uint8_t)(seq >> 16);
		f[3] = (uint8_t)(seq >> 24);
	}
}


/* Returns the sequence number the frame carries, or -1 if its bytes are not
 * exactly those of that frame. */
static long frameCheck(const uint8_t *f, uint32_t len)
{
	static uint8_t want[TX_MAX];
	uint32_t seq;

	if ((len < 4u) || (len > TX_MAX)) {
		return -1;
	}
	seq = (uint32_t)f[0] | ((uint32_t)f[1] << 8) | ((uint32_t)f[2] << 16) | ((uint32_t)f[3] << 24);
	if (frameLen(seq) != len) {
		return -1;
	}
	frameFill(want, seq, len);
	return (memcmp(want, f, len) == 0) ? (long)seq : -1;
}


/* ---- 1. round trip ------------------------------------------------------ */

static void testRoundTrip(void)
{
	static uint8_t buf[TX_BUF];
	wifibatch_t b;
	wifibatch_rd_t r;
	const uint8_t *f;
	uint8_t *slot;
	uint32_t seq = 0u, flen, iter, n, first, used;
	long got;
	int count;

	for (iter = 0; iter < 20000u; ++iter) {
		uint32_t cap = WIFIBATCH_HDR + (rnd() % TX_BUF);

		wifibatch_init(&b, buf, cap);
		first = seq;
		n = 0u;
		for (;;) {
			uint32_t len = frameLen(seq);

			slot = wifibatch_slot(&b, len);
			used = b.len;
			CHECK((slot != NULL) == ((used + wifibatch_recSize(len)) <= cap),
				"slot for %u at %u/%u: %p", len, used, cap, (void *)slot);
			if (slot == NULL) {
				break;
			}
			CHECK(((uintptr_t)slot & 3u) == 0u, "slot not 4-aligned");
			frameFill(slot, seq, len);
			wifibatch_commit(&b, len);
			seq++;
			n++;
		}
		if (n == 0u) {
			continue;
		}
		used = wifibatch_finish(&b, (iter & 1u) ? WIFIBATCH_F_DRAINED : 0u);
		CHECK(used <= cap, "batch of %u bytes in a %u-byte buffer", used, cap);

		count = wifibatch_open(&r, buf, used);
		CHECK(count == (int)n, "open: %d frames, wrote %u", count, n);
		CHECK(r.flags == ((iter & 1u) ? WIFIBATCH_F_DRAINED : 0u), "flags %u", r.flags);
		while ((f = wifibatch_next(&r, &flen)) != NULL) {
			got = frameCheck(f, flen);
			CHECK(got == (long)first, "frame %ld, expected %u", got, first);
			first++;
		}
		CHECK(first == seq, "read back up to %u, wrote up to %u", first, seq);
	}
	printf("round trip: %u frames in 20000 batches\n", seq);
}


/* ---- 2. TX credit protocol ------------------------------------------------ */

/* The daemon's wifi_batchWrite, with diag_wifiFrameTx replaced by a credit
 * window: delivers frames into the checker until `*credits` runs out. */
static long g_next_rx; /* the sequence number the "air" expects next */
static unsigned long g_dups, g_lost;

static int modelDaemonWrite(const uint8_t *data, size_t size, uint32_t *credits)
{
	wifibatch_rd_t r;
	const uint8_t *f;
	uint32_t flen;
	int count, taken = 0;
	long seq;

	count = wifibatch_open(&r, data, size);
	if (count < 0) {
		return -22;
	}
	while ((f = wifibatch_next(&r, &flen)) != NULL) {
		if (*credits == 0u) {
			break;
		}
		(*credits)--;
		seq = frameCheck(f, flen);
		if (seq < g_next_rx) {
			g_dups++;
		}
		else if (seq > g_next_rx) {
			g_lost += (unsigned long)(seq - g_next_rx);
			g_next_rx = seq + 1;
		}
		else {
			g_next_rx++;
		}
		taken++;
	}
	return taken;
}


/* The netif's wifi_txFlush, with the write() going to the model daemon. With
 * `broken` it resends the whole batch instead of dropping what was taken. */
static void modelFlush(wifibatch_t *b, int broken, unsigned long *retries)
{
	uint32_t count, credits;
	int n;

	for (;;) {
		count = b->count;
		credits = rnd() % 24u; /* the window: 0 (shut) .. 23 frames */
		n = modelDaemonWrite(b->buf, wifibatch_finish(b, 0u), &credits);
		CHECK((n >= 0) && ((uint32_t)n <= count), "write returned %d of %u", n, count);
		if ((n < 0) || ((uint32_t)n == count)) {
			return;
		}
		if (!broken) {
			wifibatch_drop(b, (uint32_t)n);
		}
		(*retries)++;
	}
}


static unsigned long runTxModel(int broken)
{
	static uint8_t bufs[2][TX_BUF];
	wifibatch_t q;
	uint8_t *slot;
	uint32_t seq = 0u, round;
	unsigned long retries = 0u;
	int which = 0;

	g_next_rx = 0;
	g_dups = 0u;
	g_lost = 0u;
	for (round = 0; round < 5000u; ++round) {
		uint32_t want = 1u + (rnd() % (TX_FRAMES + 4u));

		/* linkoutput: queue until the batch or the buffer is full */
		wifibatch_init(&q, bufs[which], TX_BUF);
		while ((want-- > 0u) && (q.count < TX_FRAMES) &&
			((slot = wifibatch_slot(&q, frameLen(seq))) != NULL)) {
			frameFill(slot, seq, frameLen(seq));
			wifibatch_commit(&q, frameLen(seq));
			seq++;
		}
		which ^= 1;
		modelFlush(&q, broken, &retries);
	}
	if (!broken) {
		printf("tx credit model: %u frames, %lu partial writes resent\n", seq, retries);
		CHECK(g_next_rx == (long)seq, "the air saw %ld of %u frames", g_next_rx, seq);
	}
	return g_dups + g_lost;
}


static void testTxCredits(void)
{
	unsigned long bad;

	/* Canary: without the drop the checker must see duplicates. */
	bad = runTxModel(1);
	CHECK(bad != 0u, "canary: a flush that resends taken frames was not caught");
	printf("canary (no drop): %lu duplicates/losses detected, as it must\n", bad);

	bad = runTxModel(0);
	CHECK(bad == 0u, "%lu duplicates, %lu lost", g_dups, g_lost);
}


/* ---- 3. RX fill ------------------------------------------------------------ */

static void testRxFill(void)
{
	static uint8_t buf[RX_BUF];
	wifibatch_t b;
	wifibatch_rd_t r;
	const uint8_t *f;
	uint8_t *slot;
	uint32_t seq = 0u, flen, first, batches = 0u, frames = 0u, maxn = 0u, i;
	long got;
	int count;

	for (i = 0; i < 20000u; ++i) {
		uint32_t queued = rnd() % 48u; /* frames in the FIFO */

		wifibatch_init(&b, buf, RX_BUF);
		first = seq;
		for (;;) {
			slot = wifibatch_slot(&b, F2_FRAME_MAX);
			if ((slot == NULL) || (b.count >= RX_FRAMES) || (queued == 0u)) {
				break;
			}
			frameFill(slot, seq, frameLen(seq));
			wifibatch_commit(&b, frameLen(seq));
			seq++;
			queued--;
		}
		if (b.count == 0u) {
			continue;
		}
		count = wifibatch_open(&r, buf, wifibatch_finish(&b, (queued == 0u) ? WIFIBATCH_F_DRAINED : 0u));
		CHECK(count == (int)(seq - first), "rx open %d", count);
		CHECK((r.flags != 0u) == (queued == 0u), "drained flag");
		while ((f = wifibatch_next(&r, &flen)) != NULL) {
			got = frameCheck(f, flen);
			CHECK(got == (long)first, "rx frame %ld, expected %u", got, first);
			first++;
		}
		batches++;
		frames += (uint32_t)count;
		if ((uint32_t)count > maxn) {
			maxn = (uint32_t)count;
		}
	}
	/* A 24 KB buffer must hold at least 10 full-MTU frames with the slot rule. */
	wifibatch_init(&b, buf, RX_BUF);
	while ((wifibatch_slot(&b, F2_FRAME_MAX) != NULL) && (b.count < RX_FRAMES)) {
		wifibatch_commit(&b, 1514u);
	}
	CHECK(b.count >= 10u, "only %u MTU frames fit a %u-byte read", b.count, RX_BUF);
	printf("rx fill: %u frames in %u reads (max %u per read); %u MTU frames per %u-byte read\n",
		frames, batches, maxn, b.count, RX_BUF);
}


/* ---- 4. malformed input ------------------------------------------------------ */

static void testMalformed(void)
{
	static uint8_t good[4096];
	wifibatch_t b;
	wifibatch_rd_t r;
	uint8_t *slot, *bad;
	uint32_t len, cut, seq;

	wifibatch_init(&b, good, sizeof(good));
	for (seq = 0; seq < 5u; ++seq) {
		uint32_t fl = 14u + seq * 37u;

		slot = wifibatch_slot(&b, fl);
		frameFill(slot, seq, fl);
		wifibatch_commit(&b, fl);
	}
	len = wifibatch_finish(&b, 0u);
	CHECK(wifibatch_open(&r, good, len) == 5, "good batch refused");

	/* Every truncation, in an exactly-sized heap buffer so ASan sees overreads. */
	for (cut = 0; cut < len; ++cut) {
		bad = malloc(cut ? cut : 1u);
		memcpy(bad, good, cut);
		CHECK(wifibatch_open(&r, bad, cut) < 0, "truncated to %u accepted", cut);
		free(bad);
	}
	/* Trailing bytes. */
	bad = malloc(len + 4u);
	memcpy(bad, good, len);
	memset(bad + len, 0, 4u);
	CHECK(wifibatch_open(&r, bad, len + 4u) < 0, "trailing bytes accepted");
	free(bad);

	bad = malloc(len);
	/* Zero count. */
	memcpy(bad, good, len);
	bad[0] = 0u;
	bad[1] = 0u;
	CHECK(wifibatch_open(&r, bad, len) < 0, "zero count accepted");
	/* Count larger than the records. */
	memcpy(bad, good, len);
	bad[0] = 6u;
	CHECK(wifibatch_open(&r, bad, len) < 0, "count 6 of 5 accepted");
	/* Count smaller: trailing records are trailing bytes. */
	memcpy(bad, good, len);
	bad[0] = 4u;
	CHECK(wifibatch_open(&r, bad, len) < 0, "count 4 of 5 accepted");
	/* Reserved field. */
	memcpy(bad, good, len);
	bad[WIFIBATCH_HDR + 2u] = 1u;
	CHECK(wifibatch_open(&r, bad, len) < 0, "reserved field accepted");
	/* Every single-record length corrupted to every value: never accepted
	 * except as the original, never read out of bounds. */
	for (seq = 0; seq < 0x10000u; ++seq) {
		memcpy(bad, good, len);
		bad[WIFIBATCH_HDR] = (uint8_t)seq;
		bad[WIFIBATCH_HDR + 1u] = (uint8_t)(seq >> 8);
		if (wifibatch_recSize(seq) == wifibatch_recSize(14u)) {
			continue; /* same record size: a different but well-formed batch */
		}
		CHECK(wifibatch_open(&r, bad, len) < 0, "first length %u accepted", seq);
	}
	free(bad);
	printf("malformed: truncations, trailing bytes, counts, reserved field, lengths refused\n");
}


int main(void)
{
	testRoundTrip();
	testTxCredits();
	testRxFill();
	testMalformed();

	if (g_fail != 0u) {
		printf("batchtest: %lu FAILURES\n", g_fail);
		return 1;
	}
	printf("batchtest: PASS\n");
	return 0;
}
