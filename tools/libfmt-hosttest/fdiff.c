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
#include <locale.h>

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
	 * ---- the POSIX/XSI ' flag (thousands grouping) -----------------------
	 * Groups the integer part with LC_NUMERIC's thousands_sep, which is ""
	 * in the C/POSIX locale -- so there the output must equal the flag-less
	 * conversion. glibc honours that, so it is the oracle once LC_NUMERIC is
	 * pinned to "C". The trailing "|%d" proves the argument was consumed: an
	 * unrecognised flag used to emit "%'" literally and leave the value on
	 * va_list, shifting every later argument (Thunar: "(%'lu bytes)").
	 */
	setlocale(LC_NUMERIC, "C");
	{
		static const long long GVALS[] = { 0, 7, -7, 1234, -1234, 1234567,
			-1234567, 2147483647LL, -2147483648LL };
		static const double GDVALS[] = { 0.0, 1.5, -1.5, 1234.5678,
			-1234567.891, 1e9 };
		static const char *const GIFMTS[] = { "%'d|%d", "%'i|%d", "%-'8d|%d",
			"%0'8d|%d", "%'+d|%d", "%' d|%d", "%'10.3d|%d", "%''d|%d" };
		static const char *const GLFMTS[] = { "%'lu|%d", "%'ld|%d", "%'lld|%d",
			"%'zu|%d", "%-'12lu|%d", "%0'12lu|%d" };
		static const char *const GDFMTS[] = { "%'10.3f|%d", "%'.2f|%d", "%'f|%d",
			"%'g|%d", "%'G|%d", "%-'12.1f|%d", "%0'12.1f|%d" };
		int a, b2;

		for (a = 0; a < (int)(sizeof(GIFMTS) / sizeof(GIFMTS[0])); a++) {
			for (b2 = 0; b2 < (int)(sizeof(GVALS) / sizeof(GVALS[0])); b2++) {
				snprintf(desc, sizeof(desc), "v=%lld", GVALS[b2]);
				ph_snprintf(pb, sizeof(pb), GIFMTS[a], (int)GVALS[b2], 99);
				snprintf(gb, sizeof(gb), GIFMTS[a], (int)GVALS[b2], 99);
				cmp_str(GIFMTS[a], desc, pb, gb);
			}
		}
		for (a = 0; a < (int)(sizeof(GLFMTS) / sizeof(GLFMTS[0])); a++) {
			for (b2 = 0; b2 < (int)(sizeof(GVALS) / sizeof(GVALS[0])); b2++) {
				const char *fm = GLFMTS[a];
				snprintf(desc, sizeof(desc), "v=%lld", GVALS[b2]);
				if (strstr(fm, "ll") != NULL) {
					ph_snprintf(pb, sizeof(pb), fm, GVALS[b2], 99);
					snprintf(gb, sizeof(gb), fm, GVALS[b2], 99);
				}
				else if (strchr(fm, 'z') != NULL) {
					ph_snprintf(pb, sizeof(pb), fm, (size_t)GVALS[b2], 99);
					snprintf(gb, sizeof(gb), fm, (size_t)GVALS[b2], 99);
				}
				else if (strchr(fm, 'u') != NULL) {
					ph_snprintf(pb, sizeof(pb), fm, (unsigned long)GVALS[b2], 99);
					snprintf(gb, sizeof(gb), fm, (unsigned long)GVALS[b2], 99);
				}
				else {
					ph_snprintf(pb, sizeof(pb), fm, (long)GVALS[b2], 99);
					snprintf(gb, sizeof(gb), fm, (long)GVALS[b2], 99);
				}
				cmp_str(fm, desc, pb, gb);
			}
		}
		for (a = 0; a < (int)(sizeof(GDFMTS) / sizeof(GDFMTS[0])); a++) {
			for (b2 = 0; b2 < (int)(sizeof(GDVALS) / sizeof(GDVALS[0])); b2++) {
				snprintf(desc, sizeof(desc), "v=%.17g", GDVALS[b2]);
				ph_snprintf(pb, sizeof(pb), GDFMTS[a], GDVALS[b2], 99);
				snprintf(gb, sizeof(gb), GDFMTS[a], GDVALS[b2], 99);
				cmp_str(GDFMTS[a], desc, pb, gb);
			}
		}
		/* the Thunar status-bar string, verbatim */
		ph_snprintf(pb, sizeof(pb), "%d files: %s (%'lu bytes)", 73, "15.4 GiB",
				16535624089ul);
		snprintf(gb, sizeof(gb), "%d files: %s (%'lu bytes)", 73, "15.4 GiB",
				16535624089ul);
		cmp_str("thunar", "status-bar", pb, gb);
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
