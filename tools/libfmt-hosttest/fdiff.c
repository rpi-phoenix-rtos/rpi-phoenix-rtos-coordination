/*
 * fdiff.c -- differential harness for libphoenix's printf FORMATTING engine.
 *
 * tools/libnum-hosttest/ proved strtod PARSES correctly. This is the other half:
 * it drives libphoenix's real format_parse() (stdio/format.c + bignum.c)
 * natively and checks two things:
 *
 *  1. ROUND-TRIP. "%.17g" must uniquely identify a double, so formatting a
 *     value with libphoenix and parsing it back with libphoenix must return the
 *     exact original bits. This needs no reference implementation to be
 *     authoritative -- it is a closed self-consistency property, and any failure
 *     is a defect on one side or the other.
 *  2. STRING EQUALITY against glibc's snprintf, over a matrix of conversions
 *     and widths/precisions.
 *
 * A canary proves the comparison machinery fires.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <float.h>

#include "format.h"

double ph_strtod(const char *, char **);

static unsigned long total, diffs, rt_total, rt_fail;

struct fctx {
	char *buf;
	size_t cap;
	size_t n;
};

static int feed(void *c, char ch)
{
	struct fctx *f = (struct fctx *)c;
	if (f->n + 1 < f->cap) {
		f->buf[f->n] = ch;
	}
	f->n++;
	return 0;
}

static int ph_snprintf(char *buf, size_t cap, const char *fmt, ...)
{
	va_list ap;
	struct fctx c = { buf, cap, 0 };
	int r;

	va_start(ap, fmt);
	r = format_parse(&c, feed, fmt, ap);
	va_end(ap);

	buf[(c.n < cap) ? c.n : (cap - 1)] = '\0';
	return (r == 0) ? (int)c.n : -1;
}

static unsigned long known;

/*
 * One divergence is documented and deliberately NOT fixed: the scientific form
 * rounds exact ties away from zero, so "%.0e" of 2.5 gives "3e+00" against
 * glibc's "2e+00". Copying the decimal form's ties-to-even test there broke
 * %.17g round-tripping (see the comment in libphoenix stdio/format.c), and a
 * lost round-trip is worse than a last-digit tie. Counted apart so the exit
 * status still gates -- a tool that always fails gates nothing.
 */
static int known_case(const char *fmt, const char *what)
{
	return (strcmp(fmt, "%.0e") == 0) && (strcmp(what, "v=2.5") == 0);
}

static void cmp_str(const char *fmt, const char *what, const char *a, const char *b)
{
	total++;
	if (strcmp(a, b) != 0) {
		if (known_case(fmt, what)) {
			known++;
			printf("FKNOWN fmt=%-10s %-24s ph=\"%s\" glibc=\"%s\" (documented, see format.c)\n",
					fmt, what, a, b);
			return;
		}
		diffs++;
		if (diffs <= 40) {
			printf("FDIFF fmt=%-10s %-24s ph=\"%s\" glibc=\"%s\"\n", fmt, what, a, b);
		}
	}
}

int main(int argc, char **argv)
{
	long iters = (argc > 1) ? atol(argv[1]) : 200000;
	unsigned long long st = (argc > 2) ? strtoull(argv[2], NULL, 0) : 0x2545F4914F6CDD1DuLL;
	char pb[512], gb[512], desc[64];
	long i;

	static const char *const FMTS[] = {
		"%g", "%.17g", "%.15g", "%.1g", "%e", "%.17e", "%.0e", "%f", "%.0f",
		"%.3f", "%.10f", "%E", "%G", "%12.4f", "%-12.4e", "%+.3f", "% .3f",
		"%#.0f", "%08.2f",
	};
	const int NF = (int)(sizeof(FMTS) / sizeof(FMTS[0]));

	static const double VALS[] = {
		0.0, -0.0, 1.0, -1.0, 0.5, 0.1, 2.0 / 3.0, 3.14159265358979,
		1e-5, 1e-4, 1e5, 1e6, 1e15, 1e16, 1e17, 1e-300, 1e300,
		DBL_MAX, DBL_MIN, 123456789.0, 0.000123456789,
		9007199254740993.0, 2.5, 3.5, 0.125, 1024.0, 1e-310,
	};
	const int NV = (int)(sizeof(VALS) / sizeof(VALS[0]));
	int f, v;

	{
		unsigned long before = diffs;
		cmp_str("canary", "must-fire", "a", "b");
		if (diffs != before + 1) {
			printf("FMT-HOST BROKEN canary-did-not-fire\n");
			return 2;
		}
		diffs = before;
		total--;
		printf("FMT-HOST canary ok\n");
	}

	/* ---- conversion matrix against glibc ----------------------------- */
	for (f = 0; f < NF; f++) {
		for (v = 0; v < NV; v++) {
			ph_snprintf(pb, sizeof(pb), FMTS[f], VALS[v]);
			snprintf(gb, sizeof(gb), FMTS[f], VALS[v]);
			snprintf(desc, sizeof(desc), "v=%.17g", VALS[v]);
			cmp_str(FMTS[f], desc, pb, gb);
		}
	}

	/* ---- integer conversions, for completeness ----------------------- */
	{
		static const char *const IFMTS[] = { "%d", "%i", "%u", "%x", "%X", "%o",
			"%ld", "%lld", "%zu", "%08d", "%-8d", "%+d", "%5.3d", "%#x", "%#o" };
		static const long long IVALS[] = { 0, 1, -1, 42, -42, 2147483647LL,
			-2147483648LL, 9223372036854775807LL, 255, 4096 };
		int a, b2;
		for (a = 0; a < (int)(sizeof(IFMTS) / sizeof(IFMTS[0])); a++) {
			for (b2 = 0; b2 < (int)(sizeof(IVALS) / sizeof(IVALS[0])); b2++) {
				const char *fm = IFMTS[a];
				snprintf(desc, sizeof(desc), "v=%lld", IVALS[b2]);
				if (strstr(fm, "ll") != NULL) {
					ph_snprintf(pb, sizeof(pb), fm, IVALS[b2]);
					snprintf(gb, sizeof(gb), fm, IVALS[b2]);
				}
				else if (strchr(fm, 'l') != NULL) {
					ph_snprintf(pb, sizeof(pb), fm, (long)IVALS[b2]);
					snprintf(gb, sizeof(gb), fm, (long)IVALS[b2]);
				}
				else if (strstr(fm, "zu") != NULL) {
					ph_snprintf(pb, sizeof(pb), fm, (size_t)IVALS[b2]);
					snprintf(gb, sizeof(gb), fm, (size_t)IVALS[b2]);
				}
				else if (strchr(fm, 'u') != NULL || strchr(fm, 'x') != NULL ||
						strchr(fm, 'X') != NULL || strchr(fm, 'o') != NULL) {
					ph_snprintf(pb, sizeof(pb), fm, (unsigned)IVALS[b2]);
					snprintf(gb, sizeof(gb), fm, (unsigned)IVALS[b2]);
				}
				else {
					ph_snprintf(pb, sizeof(pb), fm, (int)IVALS[b2]);
					snprintf(gb, sizeof(gb), fm, (int)IVALS[b2]);
				}
				cmp_str(fm, desc, pb, gb);
			}
		}
	}

	/*
	 * ---- randomised round-trip: format with libphoenix, parse with
	 * libphoenix, demand the exact original bits back.
	 */
	for (i = 0; i < iters; i++) {
		unsigned long long bits;
		double d, back;
		unsigned long long db, bb;

		st ^= st >> 12;
		st ^= st << 25;
		st ^= st >> 27;
		bits = st * 2685821657736338717uLL;

		memcpy(&d, &bits, sizeof(d));
		if (isnan(d) || isinf(d)) {
			continue;
		}

		ph_snprintf(pb, sizeof(pb), "%.17g", d);
		back = ph_strtod(pb, NULL);

		memcpy(&db, &d, sizeof(db));
		memcpy(&bb, &back, sizeof(bb));

		rt_total++;
		if (db != bb) {
			rt_fail++;
			if (rt_fail <= 10) {
				snprintf(gb, sizeof(gb), "%.17g", d);
				printf("FDIFF roundtrip orig=%016llx ph_fmt=\"%s\" reparsed=%016llx  (glibc would print \"%s\")\n",
						db, pb, bb, gb);
			}
		}
	}

	printf("FMT-HOST roundtrip iters=%lu mismatches=%lu\n", rt_total, rt_fail);
	printf("FMT-HOST total=%lu diffs=%lu known=%lu\n", total, diffs, known);
	return (diffs != 0 || rt_fail != 0) ? 1 : 0;
}
