/*
 * v3dmemprobe — cached vs uncached store cost for V3D buffer-object memory.
 *
 * Why this exists
 * ---------------
 * SuperTuxKart renders at ~1.0 fps (~1000 ms/frame) on the Pi 4, and a measured
 * frame budget (docs/misc/2026-09-08-stk-frame-budget.md) attributes only ~15%
 * to GPU command-list submits, ~2% to physics, and ~0% to the page-flip mailbox,
 * leaving ~83% -- about 825 ms -- as CPU work outside the V3D driver entirely.
 *
 * The standing hypothesis is that this is the cost of writing into buffer-object
 * memory: v3d_phoenix_winsys.c maps every BO MAP_UNCACHED by default (only
 * V3D_CREATE_BO_CACHEABLE drops it), so vertex, index, uniform, texture and
 * command-list bytes are all written through uncached stores.
 *
 * A bandwidth story does not fit on its own: 825 ms would need 50-248 MB written
 * per frame depending on the assumed rate, which a kart scene does not do. If
 * uncached memory is the cause it has to be per-store LATENCY -- roughly 4-14 M
 * non-combined stores per frame at 60-200 ns each.
 *
 * So this measures both, and keeps them apart:
 *   - memcpy          : streaming, write-combining-friendly -> a bandwidth number
 *   - sequential u32  : still contiguous, but one store instruction at a time
 *   - strided u32     : one store per 64-byte line, defeating any combining
 *                       -> the per-store latency number the hypothesis needs
 *
 * Run it on the target with no arguments. It allocates with exactly the flag
 * combinations the winsys uses, so the comparison is against real BO memory and
 * not an approximation of it.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/mman.h>

#define LINE   64u          /* A72 cache line */
#define PASSES 8u

static uint64_t now_ns(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
		return 0;
	}
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}


/* MB/s for a byte count over an elapsed span, integer math only. */
static unsigned mbps(size_t bytes, uint64_t ns)
{
	if (ns == 0) {
		return 0;
	}
	return (unsigned)((uint64_t)bytes * 1000ull / ns);   /* B/ns == GB/s; *1000 -> MB/s */
}


static void report(const char *what, const char *how, size_t bytes, uint64_t ns,
		unsigned long stores)
{
	printf("  %-9s %-14s %6u MB/s", what, how, mbps(bytes, ns));
	if (stores != 0ul) {
		printf("   %5llu ns/store", (unsigned long long)(ns / stores));
	}
	printf("   (%llu ms total)\n", (unsigned long long)(ns / 1000000ull));
}


/* One buffer, three access patterns. src is a normal cached heap buffer. */
static void bench(const char *what, volatile unsigned char *buf, size_t len,
		const unsigned char *src)
{
	volatile unsigned *u32 = (volatile unsigned *)buf;
	size_t nwords = len / sizeof(unsigned);
	uint64_t t0;
	unsigned p;
	size_t i;

	t0 = now_ns();
	for (p = 0; p < PASSES; p++) {
		memcpy((void *)buf, src, len);
	}
	report(what, "memcpy", len * PASSES, now_ns() - t0, 0ul);

	t0 = now_ns();
	for (p = 0; p < PASSES; p++) {
		for (i = 0; i < nwords; i++) {
			u32[i] = (unsigned)i;
		}
	}
	report(what, "u32 sequential", nwords * sizeof(unsigned) * PASSES, now_ns() - t0,
			(unsigned long)nwords * PASSES);

	/* One store per cache line: nothing to combine, so this exposes the true
	 * per-store cost rather than the streaming rate. */
	t0 = now_ns();
	for (p = 0; p < PASSES; p++) {
		for (i = 0; i < len; i += LINE) {
			*(volatile unsigned *)(buf + i) = (unsigned)i;
		}
	}
	report(what, "u32 per-line", (len / LINE) * sizeof(unsigned) * PASSES,
			now_ns() - t0, (unsigned long)(len / LINE) * PASSES);
}


int main(void)
{
	/* Same flags the winsys uses for a BO: contiguous either way, MAP_UNCACHED
	 * only on the default (non-CACHEABLE) path. */
	const int uncached_flags = MAP_CONTIGUOUS | MAP_UNCACHED | MAP_ANONYMOUS;
	const int cached_flags = MAP_CONTIGUOUS | MAP_ANONYMOUS;
	size_t len = 4u * 1024u * 1024u;
	void *uncached, *cached;
	unsigned char *src;

	/* Contiguous DRAM can be scarce; step down rather than fail outright. */
	for (;;) {
		uncached = mmap(NULL, len, PROT_READ | PROT_WRITE, uncached_flags, -1, 0);
		if (uncached != MAP_FAILED) {
			break;
		}
		if (len <= 256u * 1024u) {
			fprintf(stderr, "v3dmemprobe: uncached mmap failed even at %zu KiB: %s\n",
					len / 1024u, strerror(errno));
			return 1;
		}
		len /= 2u;
	}

	cached = mmap(NULL, len, PROT_READ | PROT_WRITE, cached_flags, -1, 0);
	if (cached == MAP_FAILED) {
		fprintf(stderr, "v3dmemprobe: cached mmap of %zu KiB failed: %s\n",
				len / 1024u, strerror(errno));
		munmap(uncached, len);
		return 1;
	}

	src = malloc(len);
	if (src == NULL) {
		fprintf(stderr, "v3dmemprobe: malloc(%zu) failed\n", len);
		munmap(cached, len);
		munmap(uncached, len);
		return 1;
	}
	memset(src, 0xa5, len);

	printf("v3dmemprobe: %zu KiB per buffer, %u passes, %u-byte lines\n",
			len / 1024u, PASSES, LINE);
	printf("v3dmemprobe: uncached flags 0x%x (MAP_CONTIGUOUS|MAP_UNCACHED|MAP_ANONYMOUS)\n",
			(unsigned)uncached_flags);
	printf("v3dmemprobe: cached   flags 0x%x (MAP_CONTIGUOUS|MAP_ANONYMOUS)\n",
			(unsigned)cached_flags);

	bench("CACHED", (volatile unsigned char *)cached, len, src);
	bench("UNCACHED", (volatile unsigned char *)uncached, len, src);

	printf("v3dmemprobe: done\n");

	free(src);
	munmap(cached, len);
	munmap(uncached, len);
	return 0;
}
