/*
 * libm-hosttest: ULP accuracy of libphoenix's two libm implementations.
 *
 * phx_* = libm/phoenix (default), mcs_* = libm/libmcs, plain names = the host
 * glibc (measured the same way, as a baseline). See Makefile.
 *
 * Reference: MPFR at 256 bits.
 *
 * Sections:
 *   1. random-input ULP table, double and float, per input range
 *   2. exact functions (fmod, remainder, floor, ...) vs glibc, bit for bit,
 *      on random bit patterns spanning the whole format
 *   3. Annex F special values (0, -0, inf, nan, 1, DBL_MIN, ...) vs glibc
 *   4. the SHA-256 constants test (C15): frac(p^(1/2)), frac(p^(1/3)) * 2^32
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define _GNU_SOURCE
#include <errno.h>
#include <float.h>
#include <stdarg.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mpfr.h>


typedef double (*fd1)(double);
typedef double (*fd2)(double, double);
typedef float (*ff1)(float);
typedef float (*ff2)(float, float);

#define WEAK __attribute__((weak))

#define DECL1(n) \
	extern double phx_##n(double) WEAK; \
	extern double phs_##n(double) WEAK; \
	extern double mcs_##n(double) WEAK; \
	extern float phx_##n##f(float) WEAK; \
	extern float mcs_##n##f(float) WEAK;
#define DECL2(n) \
	extern double phx_##n(double, double) WEAK; \
	extern double phs_##n(double, double) WEAK; \
	extern double mcs_##n(double, double) WEAK; \
	extern float phx_##n##f(float, float) WEAK; \
	extern float mcs_##n##f(float, float) WEAK;

DECL1(exp)
DECL1(exp2)
DECL1(expm1)
DECL1(exp10)
DECL1(log)
DECL1(log2)
DECL1(log10)
DECL1(log1p)
DECL2(pow)
DECL1(sqrt)
DECL1(cbrt)
DECL2(hypot)
DECL1(sin)
DECL1(cos)
DECL1(tan)
DECL1(asin)
DECL1(acos)
DECL1(atan)
DECL2(atan2)
DECL1(sinh)
DECL1(cosh)
DECL1(tanh)
DECL1(asinh)
DECL1(acosh)
DECL1(atanh)
DECL1(erf)
DECL1(erfc)
DECL1(lgamma)
DECL1(tgamma)
DECL2(fmod)
DECL2(remainder)

/* exact functions (section 2) */
DECL1(floor)
DECL1(ceil)
DECL1(trunc)
DECL1(round)
DECL1(rint)
DECL1(nearbyint)
DECL1(fabs)
DECL1(logb)
DECL2(copysign)
DECL2(fdim)
DECL2(fmax)
DECL2(fmin)
DECL2(nextafter)
extern double phx_frexp(double, int *) WEAK;
extern double phs_frexp(double, int *) WEAK;
extern double mcs_frexp(double, int *) WEAK;
extern double phx_modf(double, double *) WEAK;
extern double phs_modf(double, double *) WEAK;
extern double mcs_modf(double, double *) WEAK;
extern double phx_ldexp(double, int) WEAK;
extern double phs_ldexp(double, int) WEAK;
extern double mcs_ldexp(double, int) WEAK;
extern int phx_ilogb(double) WEAK;
extern int phs_ilogb(double) WEAK;
extern int mcs_ilogb(double) WEAK;


/* ------------------------------------------------------------------------ */
/* reference                                                                 */

typedef int (*mp1)(mpfr_ptr, mpfr_srcptr, mpfr_rnd_t);
typedef int (*mp2)(mpfr_ptr, mpfr_srcptr, mpfr_srcptr, mpfr_rnd_t);

static int mp_lgamma(mpfr_ptr r, mpfr_srcptr x, mpfr_rnd_t m)
{
	int s;
	return mpfr_lgamma(r, &s, x, m);
}

static mpfr_t R, T;


/* Evaluate the reference at (x, y). */
static void ref_eval(mp1 r1, mp2 r2, double x, double y)
{
	mpfr_set_d(T, x, MPFR_RNDN);
	if (r1 != NULL) {
		r1(R, T, MPFR_RNDN);
	}
	else {
		mpfr_t Y;
		mpfr_init2(Y, 64);
		mpfr_set_d(Y, y, MPFR_RNDN);
		r2(R, T, Y, MPFR_RNDN);
		mpfr_clear(Y);
	}
}


/* |got - R| in units of the last place of R in a format with `prec` bits and
 * minimum ulp exponent `emin`. A result that both sides round to the same
 * infinity/NaN is 0; any class mismatch is INFINITY. */
static double ulp_err(double got, int prec, int emin)
{
	double rd;
	long e;

	if (mpfr_nan_p(R)) {
		return isnan(got) ? 0.0 : INFINITY;
	}
	if (isnan(got)) {
		return INFINITY;
	}
	rd = (prec == 53) ? mpfr_get_d(R, MPFR_RNDN) : (double)mpfr_get_flt(R, MPFR_RNDN);
	if (isinf(rd) || isinf(got)) {
		return (rd == got) ? 0.0 : INFINITY;
	}
	if (mpfr_zero_p(R)) {
		return (got == 0.0) ? 0.0 : ldexp(fabs(got), -emin);
	}
	e = mpfr_get_exp(R) - prec;
	if (e < emin) {
		e = emin;
	}
	mpfr_set_d(T, got, MPFR_RNDN);
	mpfr_sub(T, T, R, MPFR_RNDN);
	mpfr_mul_2si(T, T, -e, MPFR_RNDN);
	return fabs(mpfr_get_d(T, MPFR_RNDN));
}


/* ------------------------------------------------------------------------ */
/* random inputs                                                             */

static uint64_t rng = 0x9e3779b97f4a7c15ULL;

static uint64_t rnd64(void)
{
	rng ^= rng << 13;
	rng ^= rng >> 7;
	rng ^= rng << 17;
	return rng;
}

static double rndu(void)
{
	return (double)(rnd64() >> 11) * 0x1p-53;
}

enum { LIN = 1, LOGP, LOGS, INTV };

struct range {
	int kind;
	double lo, hi;
};

static double gen(const struct range *r)
{
	double l, h, m;

	switch (r->kind) {
		case LIN:
			return r->lo + (r->hi - r->lo) * rndu();
		case INTV:
			return floor(r->lo + (r->hi - r->lo + 1.0) * rndu());
		default:
			l = log2(r->lo);
			h = log2(r->hi);
			m = exp2(l + (h - l) * rndu());
			if ((r->kind == LOGS) && ((rnd64() & 1) != 0)) {
				m = -m;
			}
			return m;
	}
}

static void rdesc(char *buf, size_t sz, const struct range *r)
{
	snprintf(buf, sz, "%s[%g,%g]", (r->kind == LOGP) ? "log" : (r->kind == LOGS) ? "+-log" : (r->kind == INTV) ? "int" : "", r->lo, r->hi);
}


/* ------------------------------------------------------------------------ */
/* function table                                                            */

#define MAXR 4

struct case_ {
	struct range x, y;
	double budget; /* phx max-ULP budget; < 0: informational only */
};

struct func {
	const char *name;
	mp1 r1;
	mp2 r2;
	fd1 g1, p1, m1;
	fd2 g2, p2, m2;
	ff1 gf1, pf1, mf1;
	ff2 gf2, pf2, mf2;
	struct case_ c[MAXR];
	struct case_ cf[MAXR]; /* float ranges (default: c) */
};

#define F1(n, mp) .name = #n, .r1 = (mp), .g1 = n, .p1 = phx_##n, .m1 = mcs_##n, \
		.gf1 = n##f, .pf1 = phx_##n##f, .mf1 = mcs_##n##f
#define F2(n, mp) .name = #n, .r2 = (mp), .g2 = n, .p2 = phx_##n, .m2 = mcs_##n, \
		.gf2 = n##f, .pf2 = phx_##n##f, .mf2 = mcs_##n##f

#define C1(k, a, b, bud) { { k, a, b }, { 0, 0, 0 }, bud }
#define C2(k, a, b, k2, a2, b2, bud) { { k, a, b }, { k2, a2, b2 }, bud }

/* ULP budget: glibc is <= 1 ULP on all of these (most < 0.52); 2 leaves room
 * for a correct fdlibm-class implementation without hiding a real defect. */
#define B 2.0
#define BLOOSE 4.0
#define INFO (-1.0)
#define EXACT 0.0

static const struct func funcs[] = {
	{ F1(exp, mpfr_exp), .c = { C1(LIN, -745, 709.7, B), C1(LIN, -1, 1, B), C1(LOGS, 1e-300, 1e-5, B) },
		.cf = { C1(LIN, -103, 88.7, B), C1(LIN, -1, 1, B) } },
	{ F1(exp2, mpfr_exp2), .c = { C1(LIN, -1074, 1023.9, B), C1(LIN, -1, 1, B) },
		.cf = { C1(LIN, -149, 127.9, B), C1(LIN, -1, 1, B) } },
	{ F1(expm1, mpfr_expm1), .c = { C1(LIN, -40, 709.7, B), C1(LOGS, 1e-10, 1, B) },
		.cf = { C1(LIN, -20, 88.7, B), C1(LOGS, 1e-10, 1, B) } },
	{ F1(exp10, mpfr_exp10), .c = { C1(LIN, -307, 308, B), C1(LIN, -1, 1, B) },
		.cf = { C1(LIN, -37, 38, B), C1(LIN, -1, 1, B) } },
	{ F1(log, mpfr_log), .c = { C1(LOGP, 1e-300, 1e300, B), C1(LIN, 0.5, 2, B), C1(LOGP, 1e-320, 1e-308, B) } },
	{ F1(log2, mpfr_log2), .c = { C1(LOGP, 1e-300, 1e300, B), C1(LIN, 0.5, 2, B) } },
	{ F1(log10, mpfr_log10), .c = { C1(LOGP, 1e-300, 1e300, B), C1(LIN, 0.5, 2, B) } },
	{ F1(log1p, mpfr_log1p), .c = { C1(LIN, -0.999, 1, B), C1(LOGP, 1e-300, 1e300, B), C1(LOGS, 1e-10, 0.1, B) } },
	{ F2(pow, mpfr_pow), .c = { C2(LOGP, 1e-5, 1e5, LIN, -60, 60, B), C2(LIN, 0.5, 2, LIN, -1000, 1000, B),
		C2(LIN, -10, 10, INTV, -300, 300, B), C2(LOGP, 1e-300, 1e300, LIN, -1, 1, B) },
		.cf = { C2(LOGP, 1e-3, 1e3, LIN, -12, 12, B), C2(LIN, 0.5, 2, LIN, -100, 100, B), C2(LIN, -10, 10, INTV, -30, 30, B) } },
	{ F1(sqrt, mpfr_sqrt), .c = { C1(LOGP, 1e-320, 1e300, 0.5) } },
	{ F1(cbrt, mpfr_cbrt), .c = { C1(LOGS, 1e-320, 1e300, B) } },
	{ F2(hypot, mpfr_hypot), .c = { C2(LOGS, 1e-10, 1e10, LOGS, 1e-10, 1e10, B), C2(LOGS, 1e-300, 1e300, LOGS, 1e-300, 1e300, B) } },
	{ F1(sin, mpfr_sin), .c = { C1(LIN, -3.15, 3.15, B), C1(LIN, -1e6, 1e6, B), C1(LOGS, 1e6, 1e300, B), C1(LOGS, 1e-300, 1e-3, B) } },
	{ F1(cos, mpfr_cos), .c = { C1(LIN, -3.15, 3.15, B), C1(LIN, -1e6, 1e6, B), C1(LOGS, 1e6, 1e300, B) } },
	{ F1(tan, mpfr_tan), .c = { C1(LIN, -1.58, 1.58, B), C1(LIN, -1e6, 1e6, B), C1(LOGS, 1e6, 1e300, B) } },
	{ F1(asin, mpfr_asin), .c = { C1(LIN, -1, 1, B), C1(LOGS, 1e-300, 1e-3, B) } },
	{ F1(acos, mpfr_acos), .c = { C1(LIN, -1, 1, B) } },
	{ F1(atan, mpfr_atan), .c = { C1(LOGS, 1e-300, 1e300, B), C1(LIN, -2, 2, B) } },
	{ F2(atan2, mpfr_atan2), .c = { C2(LIN, -10, 10, LIN, -10, 10, B), C2(LOGS, 1e-300, 1e300, LOGS, 1e-300, 1e300, B) } },
	{ F1(sinh, mpfr_sinh), .c = { C1(LIN, -710, 710, B), C1(LIN, -2, 2, B), C1(LOGS, 1e-300, 1e-3, B) },
		.cf = { C1(LIN, -89, 89, B), C1(LIN, -2, 2, B) } },
	{ F1(cosh, mpfr_cosh), .c = { C1(LIN, -710, 710, B), C1(LIN, -2, 2, B) },
		.cf = { C1(LIN, -89, 89, B), C1(LIN, -2, 2, B) } },
	{ F1(tanh, mpfr_tanh), .c = { C1(LIN, -20, 20, B), C1(LOGS, 1e-300, 1, B) } },
	{ F1(asinh, mpfr_asinh), .c = { C1(LOGS, 1e-300, 1e300, B), C1(LIN, -2, 2, B) } },
	{ F1(acosh, mpfr_acosh), .c = { C1(LIN, 1, 2, B), C1(LOGP, 1, 1e300, B) } },
	{ F1(atanh, mpfr_atanh), .c = { C1(LIN, -0.999999, 0.999999, B), C1(LOGS, 1e-300, 0.1, B) } },
	{ F1(erf, mpfr_erf), .c = { C1(LIN, -6, 6, B), C1(LOGS, 1e-300, 1e-3, B) } },
	{ F1(erfc, mpfr_erfc), .c = { C1(LIN, -6, 27, BLOOSE) } },
	/* lgamma near its negative zeros and tgamma at large negative x have
	 * unbounded RELATIVE error in every libm (glibc included): informational */
	{ F1(lgamma, mp_lgamma), .c = { C1(LIN, 0.01, 10, BLOOSE), C1(LOGP, 10, 1e300, BLOOSE), C1(LIN, -50, -0.01, INFO) } },
	{ F1(tgamma, mpfr_gamma), .c = { C1(LIN, 0.01, 10, BLOOSE), C1(LIN, 10, 171, BLOOSE), C1(LIN, -30, -0.01, INFO) },
		.cf = { C1(LIN, 0.01, 10, BLOOSE), C1(LIN, 10, 35, BLOOSE), C1(LIN, -30, -0.01, INFO) } },
	{ F2(fmod, mpfr_fmod), .c = { C2(LIN, -100, 100, LIN, -10, 10, EXACT), C2(LOGS, 1e-10, 1e300, LOGS, 1e-10, 1e10, EXACT) },
		.cf = { C2(LIN, -100, 100, LIN, -10, 10, EXACT), C2(LOGS, 1e-10, 1e30, LOGS, 1e-10, 1e10, EXACT) } },
	{ F2(remainder, mpfr_remainder), .c = { C2(LIN, -100, 100, LIN, -10, 10, EXACT), C2(LOGS, 1e-10, 1e300, LOGS, 1e-10, 1e10, EXACT) },
		.cf = { C2(LIN, -100, 100, LIN, -10, 10, EXACT), C2(LOGS, 1e-10, 1e30, LOGS, 1e-10, 1e10, EXACT) } },
};

#define NFUNCS (sizeof(funcs) / sizeof(funcs[0]))


/* ------------------------------------------------------------------------ */
/* section 1: random-input ULP table                                         */

struct stat_ {
	double max, sum, wx, wy;
	long n;
};

static void acc(struct stat_ *s, double e, double x, double y)
{
	if (e > s->max || (isinf(e) && !isinf(s->max))) {
		s->max = e;
		s->wx = x;
		s->wy = y;
	}
	if (isfinite(e)) {
		s->sum += e;
	}
	s->n++;
}

static void pstat(const struct stat_ *s, int present)
{
	if (!present) {
		printf(" %9s %8s", "-", "-");
	}
	else {
		printf(" %9.3g %8.3g", s->max, s->sum / (double)s->n);
	}
}

/* "x" or "x, y" in %a */
static const char *args(char *buf, size_t sz, double x, int two, double y)
{
	if (two) {
		snprintf(buf, sz, "%a, %a", x, y);
	}
	else {
		snprintf(buf, sz, "%a", x);
	}
	return buf;
}

static int failures;
static char failbuf[1 << 16];
static size_t faillen;

static void note_fail(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void note_fail(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	if (faillen < sizeof(failbuf) - 256) {
		faillen += vsnprintf(failbuf + faillen, sizeof(failbuf) - faillen, fmt, ap);
	}
	va_end(ap);
	failures++;
}

static void run_case(const struct func *f, const struct case_ *c, int isfloat, long n)
{
	struct stat_ sp = { 0 }, sm = { 0 }, sg = { 0 };
	char rd[96], ry[64], label[24];
	int two = (f->r2 != NULL);
	int hasp = isfloat ? (two ? (f->pf2 != NULL) : (f->pf1 != NULL)) : (two ? (f->p2 != NULL) : (f->p1 != NULL));
	int hasm = isfloat ? (two ? (f->mf2 != NULL) : (f->mf1 != NULL)) : (two ? (f->m2 != NULL) : (f->m1 != NULL));
	int prec = isfloat ? 24 : 53, emin = isfloat ? -149 : -1074;
	long i;

	for (i = 0; i < n; i++) {
		double x = gen(&c->x), y = two ? gen(&c->y) : 0.0;
		if (isfloat) {
			x = (float)x;
			y = (float)y;
			if (isinf(x) || isinf(y)) {
				continue;
			}
		}
		ref_eval(f->r1, f->r2, x, y);
		if (!isfloat) {
			acc(&sg, ulp_err(two ? f->g2(x, y) : f->g1(x), prec, emin), x, y);
			if (hasp) {
				acc(&sp, ulp_err(two ? f->p2(x, y) : f->p1(x), prec, emin), x, y);
			}
			if (hasm) {
				acc(&sm, ulp_err(two ? f->m2(x, y) : f->m1(x), prec, emin), x, y);
			}
		}
		else {
			acc(&sg, ulp_err(two ? f->gf2(x, y) : f->gf1(x), prec, emin), x, y);
			if (hasp) {
				acc(&sp, ulp_err(two ? f->pf2(x, y) : f->pf1(x), prec, emin), x, y);
			}
			if (hasm) {
				acc(&sm, ulp_err(two ? f->mf2(x, y) : f->mf1(x), prec, emin), x, y);
			}
		}
	}

	rdesc(rd, sizeof(rd), &c->x);
	if (two) {
		rdesc(ry, sizeof(ry), &c->y);
		strcat(rd, " x ");
		strcat(rd, ry);
	}
	snprintf(label, sizeof(label), "%s%s", f->name, isfloat ? "f" : "");
	printf("%-10s %-40s", label, rd);
	pstat(&sp, hasp);
	pstat(&sm, hasm);
	printf(" %9.3g", sg.max);

	if (!hasp) {
		printf("  MISSING\n");
		note_fail("%s%s: not provided by libm/phoenix\n", f->name, isfloat ? "f" : "");
		return;
	}
	if ((c->budget >= 0.0) && !(sp.max <= c->budget)) {
		printf("  FAIL (budget %g)\n", c->budget);
		note_fail("%s%s %s: max %.4g ULP > %g at (%s)\n", f->name, isfloat ? "f" : "", rd, sp.max, c->budget,
			args(ry, sizeof(ry), sp.wx, two, sp.wy));
	}
	else if (sp.max > 4.0) {
		printf("  (>4, informational)\n");
	}
	else {
		printf("\n");
	}
}


/* ------------------------------------------------------------------------ */
/* section 2: exact functions vs glibc                                       */

static double rbits(void)
{
	uint64_t u = rnd64();
	double d;

	/* bias towards ordinary magnitudes half of the time */
	if ((u & 3) == 0) {
		u = (u & 0x800fffffffffffffULL) | ((uint64_t)(1023 - 60 + (rnd64() % 120)) << 52);
	}
	/* signalling NaNs make fmax/fmin legitimately differ (IEEE 754-2008
	 * maxNum vs C's fmax); test quiet NaNs only */
	if (((u >> 52) & 0x7ff) == 0x7ff && (u & 0xfffffffffffffULL) != 0) {
		u |= 1ULL << 51;
	}
	memcpy(&d, &u, sizeof(d));
	return d;
}

static int same(double a, double b)
{
	if (isnan(a) || isnan(b)) {
		return isnan(a) && isnan(b);
	}
	return memcmp(&a, &b, sizeof(a)) == 0;
}

static void exact1(const char *name, fd1 g, fd1 p, fd1 ps, fd1 m, long n)
{
	long i, bp = 0, bs = 0, bm = 0;
	double wx = 0, wsx = 0;

	for (i = 0; i < n; i++) {
		double x = rbits(), r = g(x);
		if ((p != NULL) && !same(p(x), r)) {
			if (bp++ == 0) {
				wx = x;
			}
		}
		if ((ps != NULL) && !same(ps(x), r)) {
			if (bs++ == 0) {
				wsx = x;
			}
		}
		if ((m != NULL) && !same(m(x), r)) {
			bm++;
		}
	}
	printf("%-12s phx %7ld   phs %7ld   mcs ", name, bp, bs);
	if (m != NULL) {
		printf("%7ld / %ld\n", bm, n);
	}
	else {
		printf("%7s / %ld\n", "-", n);
	}
	if ((p == NULL) || (ps == NULL)) {
		note_fail("%s: not provided by libm/phoenix\n", name);
		return;
	}
	if (bp != 0) {
		note_fail("%s: %ld/%ld results differ from glibc, first x=%a phx=%a glibc=%a\n", name, bp, n, wx, p(wx), g(wx));
	}
	if (bs != 0) {
		note_fail("%s (software path): %ld/%ld results differ from glibc, first x=%a phs=%a glibc=%a\n", name, bs, n, wsx, ps(wsx), g(wsx));
	}
}

static void exact2(const char *name, fd2 g, fd2 p, fd2 ps, fd2 m, long n)
{
	long i, bp = 0, bs = 0, bm = 0;
	double wx = 0, wy = 0, wsx = 0, wsy = 0;

	for (i = 0; i < n; i++) {
		double x = rbits(), y = rbits(), r = g(x, y);
		if ((p != NULL) && !same(p(x, y), r)) {
			if (bp++ == 0) {
				wx = x;
				wy = y;
			}
		}
		if ((ps != NULL) && !same(ps(x, y), r)) {
			if (bs++ == 0) {
				wsx = x;
				wsy = y;
			}
		}
		if ((m != NULL) && !same(m(x, y), r)) {
			bm++;
		}
	}
	printf("%-12s phx %7ld   phs %7ld   mcs %7ld / %ld\n", name, bp, bs, bm, n);
	if ((p == NULL) || (ps == NULL)) {
		note_fail("%s: not provided by libm/phoenix\n", name);
		return;
	}
	if (bp != 0) {
		note_fail("%s: %ld/%ld results differ from glibc, first x=%a y=%a phx=%a glibc=%a\n", name, bp, n, wx, wy, p(wx, wy), g(wx, wy));
	}
	if (bs != 0) {
		note_fail("%s (software path): %ld/%ld results differ from glibc, first x=%a y=%a phs=%a glibc=%a\n", name, bs, n, wsx, wsy, ps(wsx, wsy), g(wsx, wsy));
	}
}

static double g_frexp_m(double x)
{
	int e;
	double m = frexp(x, &e);
	return isfinite(m) && (m != 0.0) ? m * 4096.0 + (double)e : m;
}
static double p_frexp_m(double x)
{
	int e = 0;
	double m = phx_frexp(x, &e);
	return isfinite(m) && (m != 0.0) ? m * 4096.0 + (double)e : m;
}
static double s_frexp_m(double x)
{
	int e = 0;
	double m = phs_frexp(x, &e);
	return isfinite(m) && (m != 0.0) ? m * 4096.0 + (double)e : m;
}
static double m_frexp_m(double x)
{
	int e = 0;
	double m = mcs_frexp(x, &e);
	return isfinite(m) && (m != 0.0) ? m * 4096.0 + (double)e : m;
}
static double g_modf_i(double x)
{
	double i, f = modf(x, &i);
	return isnan(f) ? f : i + 0.0 * f;
}
static double p_modf_i(double x)
{
	double i, f = phx_modf(x, &i);
	return isnan(f) ? f : i + 0.0 * f;
}
static double s_modf_i(double x)
{
	double i, f = phs_modf(x, &i);
	return isnan(f) ? f : i + 0.0 * f;
}
static double m_modf_i(double x)
{
	double i, f = mcs_modf(x, &i);
	return isnan(f) ? f : i + 0.0 * f;
}
static double g_modf_f(double x)
{
	double i;
	return modf(x, &i);
}
static double p_modf_f(double x)
{
	double i;
	return phx_modf(x, &i);
}
static double s_modf_f(double x)
{
	double i;
	return phs_modf(x, &i);
}
static double m_modf_f(double x)
{
	double i;
	return mcs_modf(x, &i);
}
static int ldexp_n(double y)
{
	uint64_t u;
	memcpy(&u, &y, sizeof(u));
	return (int)(u % 4400) - 2200;
}
static double g_ldexp(double x, double y)
{
	return ldexp(x, ldexp_n(y));
}
static double p_ldexp(double x, double y)
{
	return phx_ldexp(x, ldexp_n(y));
}
static double s_ldexp(double x, double y)
{
	return phs_ldexp(x, ldexp_n(y));
}
static double m_ldexp(double x, double y)
{
	return mcs_ldexp(x, ldexp_n(y));
}
/* ilogb(NaN) is FP_ILOGBNAN, implementation-defined: not compared */
static double g_ilogb(double x)
{
	return isnan(x) ? 0.0 : (double)ilogb(x);
}
static double p_ilogb(double x)
{
	return isnan(x) ? 0.0 : (double)phx_ilogb(x);
}
static double s_ilogb(double x)
{
	return isnan(x) ? 0.0 : (double)phs_ilogb(x);
}
static double m_ilogb(double x)
{
	return isnan(x) ? 0.0 : (double)mcs_ilogb(x);
}

#define EX1(n) exact1(#n, n, phx_##n, phs_##n, mcs_##n, nex)
#define EX2(n) exact2(#n, n, phx_##n, phs_##n, mcs_##n, nex)


/* ------------------------------------------------------------------------ */
/* section 3: Annex F special values vs glibc                                */

static const double specials[] = {
	0.0, -0.0, 1.0, -1.0, 0.5, -0.5, 2.0, -2.0, 3.0, -3.0, 1.5, -1.5,
	INFINITY, -INFINITY, NAN, DBL_MIN, -DBL_MIN, 0x1p-1074, -0x1p-1074, DBL_MAX, -DBL_MAX,
	709.8, -745.2, 1e300, -1e300, 0x1.921fb54442d18p0, 1e22, 0.1, 1e-310, 4503599627370497.0, -4503599627370497.0
};

#define NSPEC (sizeof(specials) / sizeof(specials[0]))

/* Classification match: NaN-ness, infinity and its sign, zero and its sign;
 * finite non-zero results must have the same sign and be within 4 ULP. */
static int spec_ok(double a, double g)
{
	uint64_t ua, ug;
	if (isnan(a) || isnan(g)) {
		return isnan(a) && isnan(g);
	}
	if (signbit(a) != signbit(g)) {
		return 0;
	}
	if (isinf(a) || isinf(g) || (a == 0.0) || (g == 0.0)) {
		return a == g;
	}
	memcpy(&ua, &a, 8);
	memcpy(&ug, &g, 8);
	return ((ua > ug) ? (ua - ug) : (ug - ua)) <= 4;
}

static int errclass(int e)
{
	return (e == EDOM) ? 1 : (e == ERANGE) ? 2 : 0;
}

static void specials_run(const struct func *f, int verbose)
{
	int two = (f->r2 != NULL);
	size_t i, j, nj = two ? NSPEC : 1;
	long bad = 0, ebad = 0, total = 0;
	char first[256] = "", ab[64];

	if ((two ? (void *)f->p2 : (void *)f->p1) == NULL) {
		return;
	}
	for (i = 0; i < NSPEC; i++) {
		for (j = 0; j < nj; j++) {
			double x = specials[i], y = specials[j], g, p;
			int eg, ep;

			errno = 0;
			g = two ? f->g2(x, y) : f->g1(x);
			eg = errno;
			errno = 0;
			p = two ? f->p2(x, y) : f->p1(x);
			ep = errno;
			total++;
			if (!spec_ok(p, g)) {
				if (bad++ == 0) {
					snprintf(first, sizeof(first), "%s(%s) phx=%a glibc=%a", f->name, args(ab, sizeof(ab), x, two, y), p, g);
				}
				if (verbose) {
					printf("    %s(%s) phx=%a glibc=%a\n", f->name, args(ab, sizeof(ab), x, two, y), p, g);
				}
			}
			/* errno: only a missing/wrong EDOM or a missing overflow ERANGE
			 * is counted (glibc also sets ERANGE on some underflows). */
			if ((errclass(ep) != errclass(eg)) && (errclass(eg) == 1 || (errclass(eg) == 2 && isinf(g)) || errclass(ep) == 1)) {
				ebad++;
			}
		}
	}
	printf("%-12s %4ld/%-4ld value mismatches   %4ld errno mismatches%s%s\n", f->name, bad, total, ebad, bad ? "   e.g. " : "", first);
	if (bad != 0) {
		note_fail("special values: %s: %ld mismatches, e.g. %s\n", f->name, bad, first);
	}
}


/* ------------------------------------------------------------------------ */
/* section 4: SHA-256 constants                                              */

static const uint32_t sha256_h[8] = {
	0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
};

static const uint32_t sha256_k[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

/* sjcl's precompute(): (x - Math.floor(x)) * 0x100000000 | 0 */
static uint32_t frac32(double x)
{
	return (uint32_t)(int64_t)((x - floor(x)) * 4294967296.0);
}

static int sha256_run(const char *name, fd2 powfn)
{
	int primes[64], n = 0, c = 2, i, bad = 0;

	while (n < 64) {
		int ok = 1;
		for (i = 0; (i < n) && (primes[i] * primes[i] <= c); i++) {
			if (c % primes[i] == 0) {
				ok = 0;
				break;
			}
		}
		if (ok) {
			primes[n++] = c;
		}
		c++;
	}
	for (i = 0; i < 64; i++) {
		if ((i < 8) && (frac32(powfn(primes[i], 0.5)) != sha256_h[i])) {
			bad++;
		}
		if (frac32(powfn(primes[i], 1.0 / 3.0)) != sha256_k[i]) {
			bad++;
		}
	}
	printf("%-6s %2d / 72 SHA-256 constants wrong\n", name, bad);
	return bad;
}


/* ------------------------------------------------------------------------ */

int main(int argc, char **argv)
{
	long n = (argc > 1) ? atol(argv[1]) : 20000;
	int verbose = (argc > 2);
	size_t i, k;

	mpfr_init2(R, 256);
	mpfr_init2(T, 256);
	printf("reference: MPFR %s, 256 bits\n", mpfr_get_version());
	printf("\n== 1. ULP error on random inputs (%ld per range); budget applies to phx\n\n", n);
	printf("%-10s %-40s %9s %8s %9s %8s %9s\n", "function", "range", "phx max", "mean", "mcs max", "mean", "glibc max");
	for (i = 0; i < NFUNCS; i++) {
		for (k = 0; (k < MAXR) && (funcs[i].c[k].x.kind != 0); k++) {
			run_case(&funcs[i], &funcs[i].c[k], 0, n);
		}
	}
	printf("\n");
	for (i = 0; i < NFUNCS; i++) {
		const struct case_ *cs = (funcs[i].cf[0].x.kind != 0) ? funcs[i].cf : funcs[i].c;
		for (k = 0; (k < MAXR) && (cs[k].x.kind != 0); k++) {
			run_case(&funcs[i], &cs[k], 1, n);
		}
	}

	long nex = n * 10;
	printf("\n== 2. exact functions: results differing from glibc bit-for-bit (random bit patterns)\n\n");
	EX2(fmod);
	EX2(remainder);
	EX1(floor);
	EX1(ceil);
	EX1(trunc);
	EX1(round);
	EX1(rint);
	EX1(nearbyint);
	EX1(fabs);
	EX1(logb);
	EX1(sqrt);
	EX2(copysign);
	EX2(fdim);
	EX2(fmax);
	EX2(fmin);
	EX2(nextafter);
	exact1("frexp", g_frexp_m, p_frexp_m, s_frexp_m, m_frexp_m, nex);
	exact1("modf(int)", g_modf_i, p_modf_i, s_modf_i, m_modf_i, nex);
	exact1("modf(frac)", g_modf_f, p_modf_f, s_modf_f, m_modf_f, nex);
	exact2("ldexp", g_ldexp, p_ldexp, s_ldexp, m_ldexp, nex);
	exact1("ilogb", g_ilogb, p_ilogb, s_ilogb, m_ilogb, nex);

	printf("\n== 3. Annex F special values vs glibc (phx)\n\n");
	for (i = 0; i < NFUNCS; i++) {
		specials_run(&funcs[i], verbose);
	}

	printf("\n== 4. SHA-256 constants from pow(p, 1/2), pow(p, 1/3) (JetStream stanford-crypto, C15)\n\n");
	if (sha256_run("phx", phx_pow) != 0) {
		note_fail("SHA-256 constants: phx pow is wrong\n");
	}
	sha256_run("mcs", mcs_pow);
	sha256_run("glibc", pow);

	printf("\n== RESULT: %d phx failure(s)\n%s", failures, failbuf);
	return (failures != 0) ? 1 : 0;
}
