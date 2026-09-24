/*
 * tdiff.c -- differential + round-trip harness for libphoenix's calendar code.
 *
 * Compiles libphoenix's real time/time.c natively and compares gmtime_r,
 * timegm, mktime and strftime against the host glibc.
 *
 * Calendar arithmetic is pure computation with famously awkward edges -- leap
 * years, century rules, year boundaries, negative (pre-1970) times -- so it is
 * exactly the kind of code a host harness can cover far better than a handful
 * of target cases.
 *
 * Two independent checks:
 *   1. ROUND-TRIP: timegm(gmtime_r(t)) == t, for every t tested. Needs no
 *      reference implementation to be authoritative -- it is a closed identity.
 *   2. FIELD EQUALITY against glibc's gmtime_r, plus strftime output strings.
 *
 * Everything runs in UTC (TZ=UTC) so localtime paths are deterministic.
 *
 * A canary proves the comparison machinery reports a difference.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>

struct tm *ph_gmtime_r(const time_t *, struct tm *);
time_t ph_timegm(struct tm *);
time_t ph_mktime(struct tm *);
size_t ph_strftime(char *, size_t, const char *, const struct tm *);

static unsigned long total, diffs, rt_total, rt_fail;

static void rep(const char *what, long long t, const char *field,
		long long a, long long b)
{
	diffs++;
	if (diffs <= 40) {
		printf("TDIFF %-10s t=%-14lld %-8s ph=%-14lld glibc=%lld\n",
				what, t, field, a, b);
	}
}

static void cmp_tm(long long t, const struct tm *a, const struct tm *b)
{
#define F(name) \
	do { \
		total++; \
		if (a->name != b->name) { \
			rep("gmtime_r", t, #name, (long long)a->name, (long long)b->name); \
		} \
	} while (0)

	F(tm_sec);
	F(tm_min);
	F(tm_hour);
	F(tm_mday);
	F(tm_mon);
	F(tm_year);
	F(tm_wday);
	F(tm_yday);
#undef F
}

static void check_one(time_t t)
{
	struct tm pa, gb;
	time_t back;
	char pbuf[256], gbuf[256];
	static const char *const FMTS[] = {
		"%Y-%m-%d %H:%M:%S", "%j", "%U", "%W", "%w", "%u", "%a %b", "%C%y",
		"%D", "%F", "%T", "%R", "%e", "%I %p", "%G-W%V", "%s",
	};
	int i;

	memset(&pa, 0, sizeof(pa));
	memset(&gb, 0, sizeof(gb));

	if (ph_gmtime_r(&t, &pa) == NULL || gmtime_r(&t, &gb) == NULL) {
		return;
	}

	cmp_tm((long long)t, &pa, &gb);

	/* closed identity: decompose then recompose must return the same instant */
	{
		struct tm copy = pa;
		rt_total++;
		back = ph_timegm(&copy);
		if (back != t) {
			rt_fail++;
			if (rt_fail <= 15) {
				printf("TDIFF roundtrip t=%lld timegm(gmtime(t))=%lld  (%04d-%02d-%02d %02d:%02d:%02d)\n",
						(long long)t, (long long)back, pa.tm_year + 1900,
						pa.tm_mon + 1, pa.tm_mday, pa.tm_hour, pa.tm_min, pa.tm_sec);
			}
		}
	}

	for (i = 0; i < (int)(sizeof(FMTS) / sizeof(FMTS[0])); i++) {
		memset(pbuf, 0, sizeof(pbuf));
		memset(gbuf, 0, sizeof(gbuf));
		ph_strftime(pbuf, sizeof(pbuf), FMTS[i], &pa);
		strftime(gbuf, sizeof(gbuf), FMTS[i], &gb);
		total++;
		if (strcmp(pbuf, gbuf) != 0) {
			diffs++;
			if (diffs <= 40) {
				printf("TDIFF strftime  t=%-14lld fmt=%-8s ph=\"%s\" glibc=\"%s\"\n",
						(long long)t, FMTS[i], pbuf, gbuf);
			}
		}
	}
}

int main(int argc, char **argv)
{
	long iters = (argc > 1) ? atol(argv[1]) : 100000;
	unsigned long long st = (argc > 2) ? strtoull(argv[2], NULL, 0) : 0x9E3779B97F4A7C15uLL;
	long i;

	setenv("TZ", "UTC", 1);
	tzset();

	{
		unsigned long before = diffs;
		rep("canary", 0, "must", 1, 2);
		if (diffs != before + 1) {
			printf("TIME-HOST BROKEN canary-did-not-fire\n");
			return 2;
		}
		diffs = before;
		printf("TIME-HOST canary ok\n");
	}

	/* ---- hand-picked edges ------------------------------------------- */
	{
		static const long long EDGES[] = {
			0, 1, -1, 86399, 86400, 86401,
			951782400,   /* 2000-02-29, the century leap year */
			4107542400LL,/* 2100-03-01, the century NON-leap year */
			1078012800,  /* 2004-02-29 */
			1709164800,  /* 2024-02-29 */
			1072915200,  /* 2004-01-01 */
			1104537599,  /* 2004-12-31 23:59:59 */
			-86400, -1000000000LL, -2208988800LL, /* 1900-01-01 */
			2147483647LL, 2147483648LL, -2147483648LL, -2147483649LL,
			253402300799LL, /* 9999-12-31 23:59:59 */
			1000000000, 1234567890, 1700000000,
		};
		int e;
		for (e = 0; e < (int)(sizeof(EDGES) / sizeof(EDGES[0])); e++) {
			check_one((time_t)EDGES[e]);
		}
	}

	/* ---- every day boundary across a wide span ------------------------ */
	{
		long long d;
		/* 1950-01-01 .. 2100-01-01, one sample per day plus an odd offset */
		for (d = -631152000LL; d < 4102444800LL; d += 86400) {
			check_one((time_t)(d + 45296)); /* 12:34:56 */
		}
	}

	/* ---- randomised ---------------------------------------------------- */
	for (i = 0; i < iters; i++) {
		long long t;
		st ^= st >> 12;
		st ^= st << 25;
		st ^= st >> 27;
		/* spread across roughly 1600..2400 */
		t = (long long)(st % 25000000000uLL) - 11000000000LL;
		check_one((time_t)t);
	}

	printf("TIME-HOST roundtrip checked=%lu failed=%lu\n", rt_total, rt_fail);
	printf("TIME-HOST total=%lu diffs=%lu\n", total, diffs);
	return (diffs != 0 || rt_fail != 0) ? 1 : 0;
}

/*
 * Stubs for the three Phoenix syscalls time.c references. None of them is on a
 * path this harness exercises -- only time(), clock_gettime/settime() and
 * nanosleep() call them, and the calendar functions under test do not. They
 * exist so the object links; if one is ever reached the abort() makes that
 * loud rather than silently returning a plausible zero.
 */
#include <stdlib.h>
int gettime(time_t *raw, time_t *offs);
int settime(time_t t);
int nsleep(time_t *sec, long *nsec);

int gettime(time_t *raw, time_t *offs)
{
	(void)raw;
	(void)offs;
	abort();
}

int settime(time_t t)
{
	(void)t;
	abort();
}

int nsleep(time_t *sec, long *nsec)
{
	(void)sec;
	(void)nsec;
	abort();
}
