/*
 * genrefs: correctly rounded reference values for phoenix-rtos-tests
 * libc/math/accuracy-refs.h, computed with MPFR at 256 bits.
 *
 * For every function: a few hand-picked arguments (C15's pow(p, 1/3), arguments
 * just past the old implementations' reduction boundaries, huge trig arguments,
 * tiny ones) plus NRAND deterministic random arguments per range. Doubles and
 * floats are emitted as hex literals, so the header is exact and portable.
 *
 *   make gen-refs   ->  build/accuracy-refs.h
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <mpfr.h>

#define NRAND 12

typedef int (*mp1)(mpfr_ptr, mpfr_srcptr, mpfr_rnd_t);
typedef int (*mp2)(mpfr_ptr, mpfr_srcptr, mpfr_srcptr, mpfr_rnd_t);

static int mp_lgamma(mpfr_ptr r, mpfr_srcptr x, mpfr_rnd_t m)
{
	int s;
	return mpfr_lgamma(r, &s, x, m);
}

enum { LIN = 1, LOGP, LOGS, INTV };

struct range {
	int kind;
	double lo, hi;
};

struct fn {
	const char *name;
	mp1 r1;
	mp2 r2;
	int ulps, ulpsf;          /* test budget, double / float */
	struct range x[3], y[3];  /* y used by two-argument functions */
	double fixed[8][2];       /* hand-picked (x, y); x == 0 && y == 0 terminates */
	int nofloat;
};

static const struct fn fns[] = {
	{ "exp", mpfr_exp, NULL, 1, 1, { { LIN, -745, 709.7 }, { LIN, -1, 1 } }, {}, { { 0.5 }, { -0x1.39ffc4a30860ep+8 }, { 1e-5 }, { 709.7 } } },
	{ "exp2", mpfr_exp2, NULL, 1, 1, { { LIN, -1074, 1023.9 }, { LIN, -1, 1 } }, {}, { { 0.5 }, { -0x1.ffbf449223c0cp-1 }, { 1.0 / 3 } } },
	{ "expm1", mpfr_expm1, NULL, 1, 1, { { LIN, -40, 709 }, { LOGS, 1e-10, 1 } }, {}, { { 1e-8 }, { -0.25 } } },
	{ "log", mpfr_log, NULL, 1, 1, { { LOGP, 1e-300, 1e300 }, { LIN, 0.5, 2 }, { LOGP, 1e-320, 1e-308 } }, {}, { { 0x1.003f9ff418763p+0 }, { 7 }, { 0x1p-1074 } } },
	{ "log2", mpfr_log2, NULL, 1, 1, { { LOGP, 1e-300, 1e300 }, { LIN, 0.5, 2 } }, {}, { { 0x1.19ba41c45f91cp+0 }, { 3 } } },
	{ "log10", mpfr_log10, NULL, 1, 1, { { LOGP, 1e-300, 1e300 }, { LIN, 0.5, 2 } }, {}, { { 0x1.00098fd98dec7p+0 }, { 2 } } },
	{ "log1p", mpfr_log1p, NULL, 1, 1, { { LIN, -0.999, 1 }, { LOGP, 1e-300, 1e300 }, { LOGS, 1e-10, 0.1 } }, {}, { { 1e-20 }, { 0x1.46ed40198p-14 } } },
	{ "pow", NULL, mpfr_pow, 1, 1, { { LOGP, 1e-5, 1e5 }, { LIN, 0.5, 2 }, { LIN, -10, 10 } }, { { LIN, -60, 60 }, { LIN, -300, 300 }, { INTV, -30, 30 } },
		{ { 7, 0.5 }, { 19, 1.0 / 3 }, { -2, 3 }, { 10, -5 }, { 0x1.2513ea84c0d0dp+0, -0x1.1ca2321d64387p+9 }, { 2, 0.5 } } },
	{ "sqrt", mpfr_sqrt, NULL, 0, 0, { { LOGP, 1e-320, 1e300 } }, {}, { { 2 }, { 0x1p-1074 } } },
	{ "cbrt", mpfr_cbrt, NULL, 1, 1, { { LOGS, 1e-320, 1e300 } }, {}, { { 0x0.01fffada2f9ep-1022 }, { 27 }, { -2 } } },
	{ "hypot", NULL, mpfr_hypot, 1, 1, { { LOGS, 1e-10, 1e10 }, { LOGS, 1e-300, 1e300 } }, { { LOGS, 1e-10, 1e10 }, { LOGS, 1e-300, 1e300 } }, { { 3, 4 }, { 1e300, 1e300 } } },
	{ "sin", mpfr_sin, NULL, 1, 1, { { LIN, -3.15, 3.15 }, { LIN, -1e6, 1e6 }, { LOGS, 1e6, 1e300 } }, {}, { { 0x1.927221006d0b3p+1 }, { 1e22 }, { 0x1.921fb54442d18p+0 }, { 1e-300 } } },
	{ "cos", mpfr_cos, NULL, 1, 1, { { LIN, -3.15, 3.15 }, { LIN, -1e6, 1e6 }, { LOGS, 1e6, 1e300 } }, {}, { { -0x1.9210713d14297p+0 }, { 1e22 }, { 0x1.921fb54442d18p+0 } } },
	{ "tan", mpfr_tan, NULL, 1, 1, { { LIN, -1.58, 1.58 }, { LIN, -1e6, 1e6 }, { LOGS, 1e6, 1e300 } }, {}, { { 0x1.921fb54442d18p+0 }, { 1e22 } } },
	{ "asin", mpfr_asin, NULL, 1, 1, { { LIN, -1, 1 }, { LOGS, 1e-300, 1e-3 } }, {}, { { -0x1.1593398e89p-12 }, { 0.5 } } },
	{ "acos", mpfr_acos, NULL, 1, 1, { { LIN, -1, 1 } }, {}, { { 0x1.ff43f00243ee2p-1 }, { -1 } } },
	{ "atan", mpfr_atan, NULL, 1, 1, { { LOGS, 1e-300, 1e300 }, { LIN, -2, 2 } }, {}, { { -0x1.f7c52718ee1e8p-1 }, { 1 } } },
	{ "atan2", NULL, mpfr_atan2, 2, 1, { { LIN, -10, 10 }, { LOGS, 1e-300, 1e300 } }, { { LIN, -10, 10 }, { LOGS, 1e-300, 1e300 } }, { { 1, -1 }, { 0x1.2e3e12b1276e4p+3, 0x1.34bb971602dfcp+3 } } },
	{ "sinh", mpfr_sinh, NULL, 2, 1, { { LIN, -710, 710 }, { LIN, -2, 2 }, { LOGS, 1e-300, 1e-3 } }, {}, { { 1e-10 }, { 0x1.ffa93fdb3ecb2p+0 } } },
	{ "cosh", mpfr_cosh, NULL, 2, 1, { { LIN, -710, 710 }, { LIN, -2, 2 } }, {}, { { 0x1.ff9118216bef8p-1 }, { 710 } } },
	{ "tanh", mpfr_tanh, NULL, 2, 1, { { LIN, -20, 20 }, { LOGS, 1e-300, 1 } }, {}, { { 0x1.ff8b6574393ap-1 }, { 1e-20 } } },
	{ "asinh", mpfr_asinh, NULL, 2, 1, { { LOGS, 1e-300, 1e300 }, { LIN, -2, 2 } }, {}, { { 1e300 }, { -0x1.1026ae9b76p-11 } } },
	{ "acosh", mpfr_acosh, NULL, 2, 1, { { LIN, 1, 2 }, { LOGP, 1, 1e300 } }, {}, { { 1e300 }, { 0x1.004ac62cf459cp+0 } } },
	{ "atanh", mpfr_atanh, NULL, 2, 1, { { LIN, -0.999999, 0.999999 }, { LOGS, 1e-300, 0.1 } }, {}, { { -0x1.2af0802bp-21 }, { 0.5 } } },
	{ "erf", mpfr_erf, NULL, 1, 1, { { LIN, -6, 6 }, { LOGS, 1e-300, 1e-3 } }, {}, { { 0x1.8edaa01368528p+0 }, { 1 } } },
	{ "erfc", mpfr_erfc, NULL, 3, 1, { { LIN, -6, 27 } }, {}, { { 0x1.1b66c4a423c7fp+3 }, { 1 } } },
	{ "lgamma", mp_lgamma, NULL, 3, 1, { { LIN, 0.01, 10 }, { LOGP, 10, 1e300 } }, {}, { { 0x1.ff6d72ae7d95ep+0 }, { 0.5 }, { 100 } } },
	{ "tgamma", mpfr_gamma, NULL, 3, 1, { { LIN, 0.01, 10 }, { LIN, 10, 171 }, { LIN, -30, -0.01 } }, {}, { { 0x1.2ff6e66ba0e99p+3 }, { 0.5 }, { 170.5 } } },
	{ "fmod", NULL, mpfr_fmod, 0, 0, { { LIN, -100, 100 }, { LOGS, 1e-10, 1e300 } }, { { LIN, -10, 10 }, { LOGS, 1e-10, 1e10 } },
		{ { 0x1.0822655749b3bp+993, 0x1.4126ff9c7fdd8p-32 }, { 1, 0x1p-1074 }, { 1e300, 3 } } },
	{ "remainder", NULL, mpfr_remainder, 0, 0, { { LIN, -100, 100 }, { LOGS, 1e-10, 1e300 } }, { { LIN, -10, 10 }, { LOGS, 1e-10, 1e10 } },
		{ { 1e300, 3 }, { 5, 2 }, { 7, 2 } } },
};

#define NFNS (sizeof(fns) / sizeof(fns[0]))

static uint64_t rng = 0x2545f4914f6cdd1dULL;

static double rndu(void)
{
	rng ^= rng << 13;
	rng ^= rng >> 7;
	rng ^= rng << 17;
	return (double)(rng >> 11) * 0x1p-53;
}

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
			return ((r->kind == LOGS) && (rndu() < 0.5)) ? -m : m;
	}
}

static mpfr_t X, Y, R;

/* reference at (x, y); 0 when the result is not a finite number */
static int ref(const struct fn *f, double x, double y, int isfloat, double *out)
{
	mpfr_set_d(X, x, MPFR_RNDN);
	mpfr_set_d(Y, y, MPFR_RNDN);
	if (f->r1 != NULL) {
		f->r1(R, X, MPFR_RNDN);
	}
	else {
		f->r2(R, X, Y, MPFR_RNDN);
	}
	*out = isfloat ? (double)mpfr_get_flt(R, MPFR_RNDN) : mpfr_get_d(R, MPFR_RNDN);
	return isfinite(*out);
}

static void emit(const struct fn *f, int isfloat)
{
	int two = (f->r2 != NULL);
	int k, i, n = 0;
	double x, y, r;

	if (isfloat && f->nofloat) {
		return;
	}
	printf("static const struct math_ref%s ref_%s%s[] = {\n", two ? "2" : "1", f->name, isfloat ? "f" : "");
	for (i = 0; i < 8 && (f->fixed[i][0] != 0.0 || f->fixed[i][1] != 0.0); i++) {
		x = f->fixed[i][0];
		y = f->fixed[i][1];
		if (isfloat) {
			x = (float)x;
			y = (float)y;
			if (!isfinite(x) || (x == 0.0)) {
				continue;
			}
		}
		if (ref(f, x, y, isfloat, &r)) {
			if (two) {
				printf("\t{ %a, %a, %a },\n", x, y, r);
			}
			else {
				printf("\t{ %a, %a },\n", x, r);
			}
			n++;
		}
	}
	for (k = 0; k < 3 && f->x[k].kind != 0; k++) {
		for (i = 0; i < NRAND; i++) {
			x = gen(&f->x[k]);
			y = two ? gen(&f->y[k]) : 0.0;
			if (isfloat) {
				x = (float)x;
				y = (float)y;
				if (!isfinite(x) || !isfinite(y)) {
					continue;
				}
			}
			if (ref(f, x, y, isfloat, &r) && (r != 0.0 || f->ulps == 0)) {
				if (two) {
					printf("\t{ %a, %a, %a },\n", x, y, r);
				}
				else {
					printf("\t{ %a, %a },\n", x, r);
				}
				n++;
			}
		}
	}
	printf("};\n#define REF_%s%s_ULPS %d\n\n", f->name, isfloat ? "F" : "", isfloat ? f->ulpsf : f->ulps);
}

int main(void)
{
	size_t i;

	mpfr_init2(X, 64);
	mpfr_init2(Y, 64);
	mpfr_init2(R, 256);

	printf("/*\n * Correctly rounded reference values for accuracy.c, computed with MPFR %s\n", mpfr_get_version());
	printf(" * at 256 bits. GENERATED by coord repo tools/libm-hosttest (make gen-refs) - do not edit.\n");
	printf(" * REF_<fn>_ULPS is the allowed distance in ULPs from the correctly rounded value.\n");
	printf(" *\n * SPDX-License-Identifier: BSD-3-Clause\n */\n\n");
	printf("#ifndef _TEST_MATH_ACCURACY_REFS_H\n#define _TEST_MATH_ACCURACY_REFS_H\n\n");
	printf("struct math_ref1 {\n\tdouble x, r;\n};\n\nstruct math_ref2 {\n\tdouble x, y, r;\n};\n\n");
	for (i = 0; i < NFNS; i++) {
		emit(&fns[i], 0);
		emit(&fns[i], 1);
	}
	printf("#endif\n");
	return 0;
}
