/*
 * pidiag — a one-shot Phoenix-RTOS Raspberry Pi 4 self-test / status tool.
 *
 * Opens and reads each device node the Pi 4 port created (/dev/thermal,
 * /dev/throttled, /dev/hwrng, /dev/urandom, /dev/gpio) and prints what it finds,
 * then runs two dependency-free micro-benchmarks (integer throughput and memcpy
 * bandwidth) timed with CLOCK_MONOTONIC. It is a runnable, non-graphical
 * "is everything alive?" diagnostic — it needs no X server and no network, so it
 * runs straight from psh on the NFS/SD rootfs.
 *
 * Every probe is defensive: a missing or unreadable node is reported, never
 * fatal, so the tool always completes and prints a full report.
 *
 * Build: tools/demo-apps/pidiag/build.sh (cross aarch64-phoenix, static).
 * License: MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>

static double now_s(void)
{
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
		return 0.0;
	}
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* Fill buf with up to len bytes from path; returns bytes read, or -1 on open
 * failure. Bounded retry so a slow/partial node can't hang the tool. */
static ssize_t read_node(const char *path, void *buf, size_t len)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0) {
		return -1;
	}
	size_t got = 0;
	int tries = 0;
	while (got < len && tries < 64) {
		ssize_t n = read(fd, (char *)buf + got, len - got);
		if (n > 0) {
			got += (size_t)n;
		}
		else if (n == 0) {
			break; /* EOF */
		}
		else {
			if (errno == EINTR) {
				tries++;
				continue;
			}
			break;
		}
		tries++;
	}
	close(fd);
	return (ssize_t)got;
}

static void probe_text(const char *label, const char *path)
{
	char buf[128];
	ssize_t n = read_node(path, buf, sizeof(buf) - 1);
	if (n < 0) {
		printf("  %-12s %-10s (not present)\n", label, path);
		return;
	}
	buf[n] = '\0';
	/* Trim a trailing newline for a tidy single-line report. */
	while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
		buf[--n] = '\0';
	}
	printf("  %-12s %-10s \"%s\"\n", label, path, buf);
}

static void hexn(const unsigned char *b, int n, char *out)
{
	static const char hx[] = "0123456789abcdef";
	int i;
	for (i = 0; i < n; i++) {
		out[i * 2] = hx[b[i] >> 4];
		out[i * 2 + 1] = hx[b[i] & 0xf];
	}
	out[n * 2] = '\0';
}

/* For an entropy source: read 8 bytes twice, show both + whether they differ. */
static void probe_entropy(const char *label, const char *path)
{
	unsigned char a[8], b[8];
	char ha[20], hb[20];
	ssize_t na = read_node(path, a, sizeof(a));
	if (na < 0) {
		printf("  %-12s %-10s (not present)\n", label, path);
		return;
	}
	ssize_t nb = read_node(path, b, sizeof(b));
	hexn(a, (int)(na < 8 ? na : 8), ha);
	hexn(b, (int)(nb < 8 ? nb : 8), hb);
	int differ = (na == nb && memcmp(a, b, (size_t)na) != 0);
	printf("  %-12s %-10s %s %s  (varies: %s)\n", label, path, ha, hb,
		differ ? "yes" : "NO");
}

static void probe_binary(const char *label, const char *path)
{
	unsigned char b[16];
	char hx[40];
	ssize_t n = read_node(path, b, sizeof(b));
	if (n < 0) {
		printf("  %-12s %-10s (not present)\n", label, path);
		return;
	}
	hexn(b, (int)(n < 16 ? n : 16), hx);
	printf("  %-12s %-10s %s (%ld bytes)\n", label, path, hx, (long)n);
}

static void bench_int(void)
{
	volatile unsigned long acc = 0;
	const unsigned long iters = 200UL * 1000UL * 1000UL;
	double t0 = now_s();
	unsigned long i;
	for (i = 0; i < iters; i++) {
		acc += i * 2654435761UL + 1UL;
	}
	double dt = now_s() - t0;
	(void)acc;
	if (dt > 0.0) {
		printf("  integer    %8.1f Mops/s   (%lu ops in %.2fs)\n",
			(double)iters / dt / 1e6, iters, dt);
	}
	else {
		printf("  integer    (timer unavailable)\n");
	}
}

static void bench_memcpy(void)
{
	const size_t sz = 4UL * 1024 * 1024;
	const int reps = 64;
	char *src = malloc(sz);
	char *dst = malloc(sz);
	int i;
	if (src == NULL || dst == NULL) {
		printf("  memcpy     (alloc failed)\n");
		free(src);
		free(dst);
		return;
	}
	memset(src, 0xa5, sz);
	volatile unsigned long sink = 0;
	double t0 = now_s();
	for (i = 0; i < reps; i++) {
		memcpy(dst, src, sz);
		/* Touch the destination so the copy can't be optimised away, and
		 * perturb the source so successive copies aren't provably identical. */
		sink += (unsigned char)dst[(size_t)i * 4099 % sz];
		src[(size_t)i * 7919 % sz] = (char)(i & 0xff);
	}
	double dt = now_s() - t0;
	(void)sink;
	double mb = (double)sz * reps / (1024.0 * 1024.0);
	if (dt >= 0.01) {
		printf("  memcpy     %8.1f MB/s    (%.0f MiB in %.2fs)\n", mb / dt, mb, dt);
	}
	else if (dt > 0.0) {
		printf("  memcpy     >%.0f MB/s    (%.0f MiB in <0.01s, too fast to time precisely)\n",
			mb / 0.01, mb);
	}
	else {
		printf("  memcpy     (timer unavailable)\n");
	}
	free(src);
	free(dst);
}

int main(void)
{
	printf("== Phoenix-RTOS Pi 4 self-test (pidiag) ==\n\n");

	printf("Device nodes:\n");
	probe_text("thermal", "/dev/thermal");
	probe_text("throttled", "/dev/throttled");
	probe_entropy("hwrng", "/dev/hwrng");
	probe_entropy("urandom", "/dev/urandom");
	probe_binary("gpio", "/dev/gpio");

	printf("\nMicro-benchmarks:\n");
	bench_int();
	bench_memcpy();

	printf("\npidiag: done.\n");
	return 0;
}
