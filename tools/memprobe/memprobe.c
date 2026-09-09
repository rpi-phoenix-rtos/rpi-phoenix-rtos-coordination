/*
 * memprobe — measure CPU read/write bandwidth to CACHED vs MAP_UNCACHED DRAM.
 *
 * Why this exists: the GPU X server's present spends 8.9 ms reading back a
 * damaged band, and even the no-conversion (GL_RGBA) readback of the whole screen
 * ran at only ~200 MB/s. The X server's V3D buffers are mapped MAP_UNCACHED
 * (libv3d-client.c "to match the server's default"), so the leading explanation is
 * that the readback is bounded by uncached DRAM access rather than by anything in
 * Mesa. Mapping a cached alias for the readback would be a coherency-sensitive
 * change, so measure the PRIZE first: if cached and uncached reads are similar,
 * the whole idea is dead and no risky work is warranted.
 *
 * Deliberately uses only its own anonymous memory -- no GPU, no device, nothing
 * shared -- so it cannot perturb anything and there is no coherency question.
 *
 * Copyright 2026 Phoenix Systems
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/mman.h>

#define MB           (1024u * 1024u)
#define BUF_MB       8u
#define REPS         4u

static double now_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1e6;
}

/* Sum with a volatile sink so the compiler cannot elide the loads. */
static volatile uint64_t sink;

static double read_pass(const void *p, size_t len)
{
	const uint64_t *q = (const uint64_t *)p;
	size_t n = len / sizeof(*q), i;
	uint64_t acc = 0;
	double t0 = now_ms();

	for (i = 0; i < n; i += 8) {
		acc += q[i + 0]; acc += q[i + 1]; acc += q[i + 2]; acc += q[i + 3];
		acc += q[i + 4]; acc += q[i + 5]; acc += q[i + 6]; acc += q[i + 7];
	}
	sink = acc;
	return now_ms() - t0;
}

static double memcpy_pass(void *dst, const void *src, size_t len)
{
	double t0 = now_ms();
	memcpy(dst, src, len);
	return now_ms() - t0;
}

static void run(const char *tag, int flags)
{
	size_t len = (size_t)BUF_MB * MB;
	void *p, *shadow;
	unsigned r;
	double best_rd = 1e9, best_cp = 1e9;

	p = mmap(NULL, len, PROT_READ | PROT_WRITE, flags | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED) {
		printf("memprobe: %-22s mmap FAILED (flags 0x%x)\n", tag, (unsigned)flags);
		return;
	}
	/* Destination is always ordinary cached memory, like the DDX shadow. */
	shadow = malloc(len);
	if (shadow == NULL) {
		printf("memprobe: %-22s no shadow\n", tag);
		munmap(p, len);
		return;
	}

	memset(p, 0x5a, len);
	for (r = 0; r < REPS; r++) {
		double d = read_pass(p, len);
		double c = memcpy_pass(shadow, p, len);

		if (d < best_rd) best_rd = d;
		if (c < best_cp) best_cp = c;
	}

	printf("memprobe: %-22s read %7.1f ms (%6.1f MB/s)   memcpy-out %7.1f ms (%6.1f MB/s)\n",
		tag, best_rd, (double)BUF_MB * 1000.0 / best_rd,
		best_cp, (double)BUF_MB * 1000.0 / best_cp);

	free(shadow);
	munmap(p, len);
}

int main(void)
{
	printf("memprobe: %u MiB per pass, best of %u\n", BUF_MB, REPS);
	run("cached (default)", MAP_PRIVATE);
	run("MAP_UNCACHED", MAP_PRIVATE | MAP_UNCACHED);
	run("uncached+contiguous", MAP_UNCACHED | MAP_CONTIGUOUS);
	printf("memprobe: done\n");
	return 0;
}
