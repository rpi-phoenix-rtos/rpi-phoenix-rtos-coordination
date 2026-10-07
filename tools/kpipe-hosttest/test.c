/* Host test of the kernel pipe: posix/pipe.c + posix/uchannel.c + lib/cbuffer.c */
#include "kstub.h"
#include "src/pipe.h"
#include <stdio.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

#define RD O_RDONLY
#define WR O_WRONLY
#define NB O_NONBLOCK

static uchannel_t *mk(void)
{
	uchannel_t *ch;
	oid_t oid;
	CHECK(pipe_create(&ch, &oid) == EOK);
	CHECK(oid.port == POSIX_PORT_PIPE);
	uchannel_ref(ch); /* the second end */
	return ch;
}

static void basic(void)
{
	uchannel_t *ch = mk();
	char buf[64];

	CHECK(pipe_read(ch, buf, sizeof(buf), RD | NB) == -EWOULDBLOCK);
	CHECK(pipe_read(ch, buf, 0, RD) == 0);
	CHECK(pipe_write(ch, "hello", 5, WR) == 5);
	CHECK(pipe_avail(ch) == 5);
	CHECK((pipe_poll(ch, RD, POLLIN) & POLLIN) != 0);
	CHECK((pipe_poll(ch, RD, POLLIN) & POLLHUP) == 0);
	CHECK(pipe_poll(ch, WR, POLLOUT) == POLLOUT);
	pipe_close(ch, WR);
	/* data first, then EOF; POLLIN|POLLHUP meanwhile */
	CHECK(pipe_poll(ch, RD, POLLIN) == (POLLIN | POLLHUP));
	CHECK(pipe_read(ch, buf, 3, RD) == 3 && memcmp(buf, "hel", 3) == 0);
	CHECK(pipe_read(ch, buf, sizeof(buf), RD) == 2 && memcmp(buf, "lo", 2) == 0);
	CHECK(pipe_poll(ch, RD, POLLIN) == POLLHUP);
	CHECK(pipe_read(ch, buf, sizeof(buf), RD) == 0);
	CHECK(pipe_read(ch, buf, sizeof(buf), RD | NB) == 0);
	pipe_close(ch, RD);
	CHECK(kstub_bytes == 0);
}

static void epipe(void)
{
	uchannel_t *ch = mk();

	CHECK(pipe_write(ch, "x", 1, WR) == 1);
	pipe_close(ch, RD);
	CHECK(pipe_poll(ch, WR, POLLOUT) == (POLLOUT | POLLERR));
	CHECK(pipe_write(ch, "x", 1, WR) == -EPIPE);
	CHECK(pipe_write(ch, "x", 1, WR | NB) == -EPIPE);
	CHECK(pipe_write(ch, "x", 0, WR) == 0); /* Linux: no EPIPE for nothing */
	pipe_close(ch, WR);
	CHECK(kstub_bytes == 0);
}

static void growAtomic(void)
{
	static char big[128 * 1024], out[128 * 1024];
	uchannel_t *ch = mk();
	ssize_t r;
	size_t i, total;

	for (i = 0; i < sizeof(big); i++) {
		big[i] = (char)(i * 7 + 3);
	}

	CHECK(uchannel_size(ch) == 4096);
	/* a non-atomic write grows the ring as far as needed */
	CHECK(pipe_write(ch, big, 10000, WR | NB) == 10000);
	CHECK(uchannel_size(ch) == 16384);
	/* a big non-blocking write: partial, up to 64 kB, then EAGAIN */
	r = pipe_write(ch, big + 10000, sizeof(big) - 10000, WR | NB);
	CHECK(r == 65536 - 10000);
	CHECK(uchannel_size(ch) == 65536);
	CHECK(pipe_avail(ch) == 65536);
	CHECK(pipe_write(ch, big, 1, WR | NB) == -EWOULDBLOCK);
	CHECK(pipe_poll(ch, WR, POLLOUT) == 0);

	/* free 3000: an atomic 512 fits, a 4096 does not and must not be split */
	total = 0;
	r = pipe_read(ch, out, 3000, RD);
	CHECK(r == 3000);
	total += (size_t)r;
	CHECK(pipe_poll(ch, WR, POLLOUT) == 0); /* < PIPE_ATOMIC free */
	CHECK(pipe_write(ch, big, 4096, WR | NB) == -EWOULDBLOCK);
	CHECK(pipe_avail(ch) == 65536 - 3000);
	CHECK(pipe_write(ch, big + 65536, 512, WR | NB) == 512);
	/* a non-atomic (> PIPE_ATOMIC) non-blocking write takes what fits */
	CHECK(pipe_write(ch, big + 65536 + 512, 5000, WR | NB) == 3000 - 512);
	CHECK(pipe_avail(ch) == 65536);

	r = pipe_read(ch, out + total, 5000, RD);
	CHECK(r == 5000);
	total += (size_t)r;
	CHECK(pipe_poll(ch, WR, POLLOUT) == POLLOUT);
	CHECK(pipe_write(ch, big + 65536 + 3000, 4096, WR | NB) == 4096);

	/* drain and compare: the bytes come out in order */
	while ((r = pipe_read(ch, out + total, sizeof(out) - total, RD | NB)) > 0) {
		total += (size_t)r;
	}
	CHECK(r == -EWOULDBLOCK);
	CHECK(total == 65536 + 3000 + 4096);
	CHECK(memcmp(out, big, total) == 0);

	/* allocation failure: the write stays within the ring it has */
	pipe_close(ch, WR);
	pipe_close(ch, RD);
	CHECK(kstub_bytes == 0);

	ch = mk();
	kstub_failAlloc = 1000;
	CHECK(pipe_write(ch, big, 10000, WR | NB) == 4096);
	kstub_failAlloc = 0;
	CHECK(uchannel_size(ch) == 4096);
	CHECK(pipe_poll(ch, WR, POLLOUT) == POLLOUT); /* it may still grow */
	CHECK(pipe_write(ch, big, 100, WR | NB) == 100);
	CHECK(uchannel_size(ch) == 8192);
	pipe_close(ch, WR);
	pipe_close(ch, RD);
	CHECK(kstub_bytes == 0);
	CHECK(kstub_notifiesUnix == 0);
}


/* Writers send whole records of a fixed size, each filled with one tag; a
 * reader reads odd-sized pieces and checks no record was split by another. */
#define NWRITERS 4
#define NRECS    3000

typedef struct {
	uchannel_t *ch;
	int id;
	size_t recsz;
	unsigned status;
} wargs_t;

static void *writer(void *arg)
{
	wargs_t *a = arg;
	unsigned char rec[PIPE_ATOMIC];
	int i;

	for (i = 0; i < NRECS; i++) {
		memset(rec, (unsigned char)(a->id * 64 + (i % 64)), a->recsz);
		for (;;) {
			ssize_t r = pipe_write(a->ch, rec, a->recsz, a->status);
			if (r == -EWOULDBLOCK) {
				sched_yield();
				continue;
			}
			if (r != (ssize_t)a->recsz) {
				printf("writer %d: r=%zd\n", a->id, r);
				abort();
			}
			break;
		}
	}
	return NULL;
}

static void atomicity(size_t recsz, unsigned wstatus, unsigned rstatus)
{
	uchannel_t *ch = mk();
	pthread_t t[NWRITERS];
	wargs_t a[NWRITERS];
	static unsigned char buf[PIPE_ATOMIC * 2];
	static unsigned char rec[PIPE_ATOMIC];
	size_t fill = 0, total = 0, chunk = 777;
	unsigned bad = 0, eagain = 0;
	ssize_t r;
	int i;

	for (i = 0; i < NWRITERS; i++) {
		a[i].ch = ch;
		a[i].id = i;
		a[i].recsz = recsz;
		a[i].status = WR | wstatus;
		pthread_create(&t[i], NULL, writer, &a[i]);
	}

	for (;;) {
		r = pipe_read(ch, rec + fill, min(chunk, recsz - fill), RD | rstatus);
		if (r == -EWOULDBLOCK) {
			eagain++;
			sched_yield();
			if (total == (size_t)NWRITERS * NRECS * recsz) {
				break;
			}
			continue;
		}
		if (r <= 0) {
			break;
		}
		fill += (size_t)r;
		total += (size_t)r;
		chunk = (chunk == 777) ? 1500 : 777;
		if (fill == recsz) {
			for (size_t k = 1; k < recsz; k++) {
				if (rec[k] != rec[0]) {
					bad++;
					break;
				}
			}
			fill = 0;
		}
		if (total == (size_t)NWRITERS * NRECS * recsz) {
			break;
		}
	}

	for (i = 0; i < NWRITERS; i++) {
		pthread_join(t[i], NULL);
	}
	(void)buf;
	printf("atomicity rec=%zu w=%s r=%s: total=%zu bad=%u size=%zu\n", recsz,
		(wstatus & NB) ? "nb" : "blk", (rstatus & NB) ? "nb" : "blk", total, bad, uchannel_size(ch));
	CHECK(total == (size_t)NWRITERS * NRECS * recsz);
	CHECK(bad == 0);
	pipe_close(ch, WR);
	CHECK(pipe_read(ch, rec, 1, RD) == 0);
	pipe_close(ch, RD);
	CHECK(kstub_bytes == 0);
}


static uchannel_t *g_ch;

static void *bigWriter(void *arg)
{
	size_t n = *(size_t *)arg;
	unsigned char *p = malloc(n);
	for (size_t i = 0; i < n; i++) {
		p[i] = (unsigned char)(i % 251);
	}
	ssize_t r = pipe_write(g_ch, p, n, WR); /* one blocking write completes whole */
	if (r != (ssize_t)n) {
		printf("bigWriter r=%zd\n", r);
		fails++;
	}
	pipe_close(g_ch, WR);
	free(p);
	return NULL;
}

static void bulk(void)
{
	static unsigned char buf[3001];
	size_t n = 8U << 20, total = 0;
	unsigned bad = 0;
	pthread_t t;
	ssize_t r;

	g_ch = mk();
	pthread_create(&t, NULL, bigWriter, &n);
	while ((r = pipe_read(g_ch, buf, sizeof(buf), RD)) > 0) {
		for (ssize_t i = 0; i < r; i++) {
			if (buf[i] != (unsigned char)((total + (size_t)i) % 251)) {
				bad++;
			}
		}
		total += (size_t)r;
	}
	pthread_join(t, NULL);
	printf("bulk: total=%zu bad=%u r=%zd\n", total, bad, r);
	CHECK(r == 0 && total == n && bad == 0);
	pipe_close(g_ch, RD);
	CHECK(kstub_bytes == 0);
}


static void *closer(void *arg)
{
	usleep(100000);
	pipe_close(g_ch, *(unsigned *)arg);
	return NULL;
}

static void wakeups(void)
{
	static char buf[70000];
	unsigned end;
	pthread_t t;

	/* a blocked reader gets EOF when the last writer goes */
	g_ch = mk();
	end = WR;
	pthread_create(&t, NULL, closer, &end);
	CHECK(pipe_read(g_ch, buf, 10, RD) == 0);
	pthread_join(t, NULL);
	pipe_close(g_ch, RD);

	/* a blocked writer gets EPIPE (or its partial count) when the reader goes */
	g_ch = mk();
	end = RD;
	pthread_create(&t, NULL, closer, &end);
	CHECK(pipe_write(g_ch, buf, 65536, WR) == 65536); /* fits whole */
	CHECK(pipe_write(g_ch, buf, 100, WR) == -EPIPE);   /* blocked, then EPIPE */
	pthread_join(t, NULL);
	pipe_close(g_ch, WR);
	CHECK(kstub_bytes == 0);
}


int main(void)
{
	basic();
	epipe();
	growAtomic();
	atomicity(512, 0, 0);
	atomicity(PIPE_ATOMIC, 0, 0);
	atomicity(PIPE_ATOMIC, NB, NB);
	atomicity(1000, NB, 0);
	bulk();
	wakeups();
	printf("notifies=%u notifiesUnix=%u\n", kstub_notifies, kstub_notifiesUnix);
	CHECK(kstub_notifiesUnix == 0);
	printf("%s (%d failures)\n", (fails == 0) ? "PASS" : "FAIL", fails);
	return (fails == 0) ? 0 : 1;
}
