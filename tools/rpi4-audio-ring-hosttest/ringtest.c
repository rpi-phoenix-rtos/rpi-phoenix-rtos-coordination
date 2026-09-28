/*
 * ringtest - host test for rpi4-audio's streaming-ring bookkeeping
 *
 * Compiles the driver's own audio/rpi4-audio/rpi4-audio-ring.h (no copy) and drives
 * it against a model of the hardware: a DMA engine that reads one word per tick from
 * a ring it loops over forever and never stops, the driver's writer, and the
 * driver's sweeper. Every word the model DMA plays is checked:
 *
 *   - silence, or
 *   - the NEXT token the writer produced: in order, never twice (a replay is the
 *     bug), never skipped, and on the word parity (PWM channel) it was written for.
 *
 * A checker that cannot fail proves nothing, so the same checker is first run
 * against a model of the OLD driver (no silencing, write position never resynced)
 * and must report the replay. Then the new code must pass it, including a
 * starved sweeper (a lap between services), a parked engine, and a writer that
 * comes and goes.
 *
 * Copyright 2026 Phoenix Systems
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rpi4-audio-ring.h"

#define SIZE    16384u /* the driver's RING_WORDS */
#define SILENCE 306u   /* the driver's DUTY_SILENCE */
#define LEAD    256u   /* the driver's RESYNC_LEAD_WORDS */
#define TOKEN0  1000u  /* tokens are TOKEN0 + seq, never equal to SILENCE */

static uint32_t ring[SIZE];
static int failures;

#define CHECK(cond, ...) \
	do { \
		if (!(cond)) { \
			printf("FAIL %s:%d: ", __FILE__, __LINE__); \
			printf(__VA_ARGS__); \
			printf("\n"); \
			failures++; \
		} \
	} while (0)


/* ---- the played-stream checker ---------------------------------------------- */

typedef struct {
	uint32_t next;     /* next token seq expected on the jack */
	uint32_t replays;  /* tokens played again (seq < next) -- the bug */
	uint32_t skips;    /* tokens never played (seq > next) */
	uint32_t parity;   /* tokens on the wrong channel */
	uint32_t played;   /* tokens played in order */
} checker_t;


static void check_word(checker_t *c, uint32_t pos, uint32_t v)
{
	uint32_t seq;

	if (v == SILENCE) {
		return;
	}
	seq = v - TOKEN0;
	if (seq < c->next) {
		c->replays++;
		return;
	}
	if (seq > c->next) {
		c->skips++;
	}
	if ((seq & 1u) != (pos & 1u)) {
		c->parity++;
	}
	c->next = seq + 1u;
	c->played++;
}


/* ---- the model ---------------------------------------------------------------- */

typedef struct {
	audioring_t rs;
	uint32_t dma;         /* model DMA read cursor */
	uint64_t tick;        /* one tick = one word played */
	uint64_t svcTick;     /* tick of the last service */
	uint32_t seq;         /* next token the writer produces */
	int parked;           /* the engine stopped consuming */
	int old;              /* model the pre-fix driver */
	uint32_t drains, laps;
	checker_t chk;
} model_t;


static void model_init(model_t *m, int old)
{
	memset(m, 0, sizeof(*m));
	(void)audioring_fill(ring, SIZE, 0, SIZE, SILENCE);
	audioring_init(&m->rs, SIZE);
	m->old = old;
	/* The engine has already moved during the arm settle, as on hardware. */
	m->dma = 1784u;
}


/* The driver's audio_ringService(), with ticks for gettime(). */
static void model_service(model_t *m)
{
	uint32_t filled, flags, consumed;

	if (m->old != 0) {
		return;
	}
	consumed = audioring_consumed(&m->rs, m->dma, m->tick - m->svcTick);
	flags = audioring_advance(&m->rs, ring, m->dma, consumed, SILENCE, &filled);
	m->svcTick = m->tick;
	if ((flags & AUDIORING_DRAINED) != 0u) {
		m->drains++;
	}
	if ((flags & AUDIORING_LAPPED) != 0u) {
		m->laps++;
	}
}


/* The old driver's backpressure test, verbatim in effect:
 * ((write_idx - readIdx + RING) % RING) >= RING - 1  ->  wait. */
static uint32_t old_space(const model_t *m)
{
	uint32_t lag = (m->rs.wr + SIZE - m->dma) % SIZE;
	return (SIZE - 1u) - lag;
}


/* The driver's audio_ringWrite() for up to `n` samples; returns how many it took
 * without blocking (the model's writer comes back on a later tick for the rest). */
static uint32_t model_write(model_t *m, uint32_t n)
{
	uint32_t k, i;

	model_service(m);
	if (m->old == 0) {
		audioring_resync(&m->rs, LEAD);
	}
	k = (m->old != 0) ? old_space(m) : audioring_space(&m->rs);
	if (k > n) {
		k = n;
	}
	for (i = 0; i < k; i++) {
		ring[(m->rs.wr + i) % SIZE] = TOKEN0 + m->seq++;
	}
	if (m->old != 0) {
		m->rs.wr = (m->rs.wr + k) % SIZE;
	}
	else {
		audioring_commit(&m->rs, k);
	}
	return k;
}


static void model_tick(model_t *m)
{
	if (m->parked == 0) {
		check_word(&m->chk, m->dma, ring[m->dma]);
		m->dma = (m->dma + 1u) % SIZE;
	}
	m->tick++;
}


/* Run for `ticks`, the writer offering `chunk` samples every `wperiod` ticks while
 * `write` is set (and each time it wakes it takes what fits, like the driver's
 * blocking loop), the sweeper servicing every `speriod` ticks while anything is
 * pending (0 = sweeper starved). */
static void model_run(model_t *m, uint64_t ticks, int write, uint32_t chunk, uint32_t wperiod,
	uint32_t speriod)
{
	uint64_t t;
	uint32_t owed = 0;

	for (t = 0; t < ticks; t++) {
		if (write != 0) {
			if ((t % wperiod) == 0u) {
				owed += chunk;
			}
			if (owed != 0u) {
				owed -= model_write(m, owed);
			}
		}
		if ((speriod != 0u) && ((t % speriod) == 0u) && (m->rs.pending != 0u)) {
			model_service(m);
		}
		model_tick(m);
	}
}


/* ---- tests -------------------------------------------------------------------- */

static void test_fill(void)
{
	uint32_t r[8], i, n;

	for (i = 0; i < 8u; i++) {
		r[i] = 7u;
	}
	n = audioring_fill(r, 8u, 3u, 0u, 1u);
	CHECK(n == 0u, "n=0 wrote %u", n);
	for (i = 0; i < 8u; i++) {
		CHECK(r[i] == 7u, "n=0 touched word %u", i);
	}

	n = audioring_fill(r, 8u, 6u, 4u, 1u); /* wraps: 6 7 0 1 */
	CHECK(n == 4u, "wrap wrote %u", n);
	CHECK((r[6] == 1u) && (r[7] == 1u) && (r[0] == 1u) && (r[1] == 1u), "wrap missed a word");
	CHECK((r[2] == 7u) && (r[5] == 7u), "wrap overran");

	n = audioring_fill(r, 8u, 5u, 8u, 2u); /* a full lap from anywhere = all of it */
	CHECK(n == 8u, "full lap wrote %u", n);
	for (i = 0; i < 8u; i++) {
		CHECK(r[i] == 2u, "full lap missed word %u", i);
	}

	n = audioring_fill(r, 8u, 13u, 100u, 3u); /* start folded, n clamped to one lap */
	CHECK(n == 8u, "clamp wrote %u", n);
	CHECK(audioring_countNot(r, 8u, 3u) == 0u, "clamp left %u words", audioring_countNot(r, 8u, 3u));
}


static void test_consumed(void)
{
	audioring_t r;

	audioring_init(&r, SIZE);
	r.rd = 100u;
	CHECK(audioring_consumed(&r, 150u, 50u) == 50u, "plain move");
	CHECK(audioring_consumed(&r, 50u, SIZE - 50u) == SIZE - 50u, "move across the wrap");
	/* 50 words by index, but the clock says a lap and 50: a lap. */
	CHECK(audioring_consumed(&r, 150u, SIZE + 50u) == SIZE, "lap not seen");
	/* Clock jitter well under half a lap must not read as a lap. */
	CHECK(audioring_consumed(&r, 150u, 50u + SIZE / 2u - 1u) == 50u, "jitter read as a lap");
	/* A cursor that did not move is parked, however long it has been. */
	CHECK(audioring_consumed(&r, 100u, 10u * SIZE) == 0u, "parked engine read as a lap");
	/* A cursor at SIZE (the CB reload instant) is word 0. */
	r.rd = SIZE - 10u;
	CHECK(audioring_consumed(&r, SIZE, 10u) == 10u, "cursor == size not folded");
}


static void test_resync_parity(void)
{
	audioring_t r;

	audioring_init(&r, SIZE);
	r.wr = 1u; /* the writer stopped after an odd number of samples */
	r.rd = 1000u;
	audioring_resync(&r, LEAD);
	CHECK((r.wr & 1u) == 1u, "resync lost the channel: wr=%u", r.wr);
	CHECK((r.wr + SIZE - 1000u) % SIZE >= LEAD, "resync inside the lead: wr=%u", r.wr);
	CHECK(r.pending == (r.wr + SIZE - 1000u) % SIZE, "the lead is not pending: pending=%u",
		r.pending);
	r.pending = 0;
	r.wr = 2u;
	r.rd = SIZE - 10u;
	audioring_resync(&r, LEAD); /* lands across the wrap */
	CHECK((r.wr & 1u) == 0u, "resync across the wrap lost the channel: wr=%u", r.wr);
	CHECK(r.pending == LEAD, "resync across the wrap: pending=%u", r.pending);
	/* With data still queued the write position is live: leave it alone. */
	audioring_resync(&r, LEAD);
	CHECK(r.pending == LEAD, "resync moved a live write position");
}


/* The canary: the pre-fix driver, a writer that plays ~0.1 s and stops. The checker
 * must see the tail again, lap after lap. If it does not, the checker is blind. */
static void test_canary_old_driver(void)
{
	model_t m;

	model_init(&m, 1);
	model_run(&m, 20000u, 1, 1024u, 1024u, 20u * 88u);
	model_run(&m, 4u * SIZE, 0, 0, 1, 20u * 88u);
	CHECK(m.chk.replays != 0u, "CANARY: the checker did not see the old driver replay "
		"its ring -- every other result here is meaningless");
	printf("canary: old driver replayed %u tokens after the writer stopped (expected > 0)\n",
		m.chk.replays);
}


/* An app plays, stops without closing (pause, underrun, exit): tail once, then silence. */
static void test_stop(uint32_t chunk, uint32_t wperiod, const char *what)
{
	model_t m;
	uint32_t written;

	model_init(&m, 0);
	model_run(&m, 3u * SIZE, 1, chunk, wperiod, 20u * 88u);
	written = m.seq;
	model_run(&m, 4u * SIZE, 0, 0, 1, 20u * 88u);

	CHECK(m.chk.replays == 0u, "%s: %u tokens replayed", what, m.chk.replays);
	CHECK(m.chk.skips == 0u, "%s: %u tokens skipped", what, m.chk.skips);
	CHECK(m.chk.parity == 0u, "%s: %u tokens on the wrong channel", what, m.chk.parity);
	CHECK(m.chk.played == written, "%s: played %u of %u tokens", what, m.chk.played, written);
	CHECK(m.rs.pending == 0u, "%s: pending=%u after the drain", what, m.rs.pending);
	CHECK(audioring_countNot(ring, SIZE, SILENCE) == 0u, "%s: %u non-silent words after the drain",
		what, audioring_countNot(ring, SIZE, SILENCE));
	CHECK(m.drains != 0u, "%s: no drain event", what);
	printf("%s: %u tokens played once each, %u drain event(s), ring silent\n", what,
		m.chk.played, m.drains);
}


/* A writer that underruns repeatedly and comes back: every token once, in order. */
static void test_stutter(void)
{
	model_t m;
	int i;

	model_init(&m, 0);
	for (i = 0; i < 12; i++) {
		model_run(&m, 5000u + 1500u * (uint32_t)i, 1, 999u, 700u, 20u * 88u);
		model_run(&m, 3000u + 7000u * (uint32_t)(i % 3), 0, 0, 1, 20u * 88u);
	}
	model_run(&m, 3u * SIZE, 0, 0, 1, 20u * 88u);

	CHECK(m.chk.replays == 0u, "stutter: %u tokens replayed", m.chk.replays);
	CHECK(m.chk.skips == 0u, "stutter: %u tokens skipped", m.chk.skips);
	CHECK(m.chk.parity == 0u, "stutter: %u tokens on the wrong channel", m.chk.parity);
	CHECK(m.chk.played == m.seq, "stutter: played %u of %u", m.chk.played, m.seq);
	CHECK(audioring_countNot(ring, SIZE, SILENCE) == 0u, "stutter: ring not silent");
	CHECK(m.laps == 0u, "stutter: %u false laps", m.laps);
	printf("stutter: %u tokens across 12 writer gaps, %u of them drained the ring, 0 laps\n", m.chk.played,
		m.drains);
}


/* The sweeper starved for more than a lap: stale audio may repeat once (it cannot be
 * un-played), but the service after that must see the lap and leave the ring silent. */
static void test_starved_sweeper(void)
{
	model_t m;

	model_init(&m, 0);
	model_run(&m, 2u * SIZE, 1, 1024u, 1024u, 20u * 88u);
	model_run(&m, SIZE + SIZE / 2u, 0, 0, 1, 0); /* no service for 1.5 laps */
	model_service(&m);
	CHECK(m.laps == 1u, "starved: laps=%u, want 1", m.laps);
	CHECK(audioring_countNot(ring, SIZE, SILENCE) == 0u, "starved: ring not silent after the lap");
	m.chk.next = m.seq; /* whatever replayed during the starvation is accounted for */
	m.chk.replays = 0;
	model_run(&m, 2u * SIZE, 0, 0, 1, 20u * 88u);
	CHECK(m.chk.replays == 0u, "starved: %u tokens replayed after the lapped service",
		m.chk.replays);
	printf("starved sweeper: lap detected, ring silenced, nothing replayed afterwards\n");
}


/* A parked engine must keep the ring FULL, so the write path's 10 s stall detector
 * (which waits for a full ring that never drains) can still fire. */
static void test_parked(void)
{
	model_t m;
	uint32_t k;

	model_init(&m, 0);
	model_run(&m, SIZE, 1, 1024u, 1024u, 20u * 88u);
	m.parked = 1;
	model_run(&m, 4u * SIZE, 1, 1024u, 1024u, 20u * 88u); /* clock runs, cursor does not */
	k = model_write(&m, 1u);
	CHECK(k == 0u, "parked: writer found %u words of space", k);
	CHECK(m.laps == 0u, "parked: %u false laps", m.laps);
	printf("parked engine: ring stays full (space 0), no false lap\n");
}


int main(void)
{
	test_fill();
	test_consumed();
	test_resync_parity();
	test_canary_old_driver();
	test_stop(1024u, 1024u, "stop, writer at playback rate");
	test_stop(4096u, 4096u, "stop, big buffers");
	test_stop(64u, 32u, "stop, writer faster than playback");
	test_stutter();
	test_starved_sweeper();
	test_parked();

	if (failures != 0) {
		printf("ringtest: %d FAILURE(S)\n", failures);
		return 1;
	}
	printf("ringtest: all passed\n");
	return 0;
}
