/*
 * v3d_phoenix_mathshim.c — REAL (out-of-line) definitions of the C99 float-math
 * functions Phoenix libm lacks, for the Mesa GL frontend (GLQuake Path C, Phase 4).
 *
 * phoenix_mesa_compat.h provides these as `static inline` for in-TU use, but
 * cross-TU external references survive: objects in the prebuilt libv3d-phoenix.a,
 * and C++ TUs whose <cmath> DECLARES expf/logf (so the inline is suppressed) yet
 * Phoenix libm has no symbol. This file provides the global symbols so any such
 * reference links. Compiled WITHOUT the compat shim (so these are definitions, not
 * conflicting inlines). Wraps Phoenix libm's double routines.
 *
 * Copyright 2026 Phoenix Systems  %LICENSE%
 */
extern double sin(double), cos(double), tan(double), asin(double), acos(double);
extern double atan(double), atan2(double, double), exp(double), log(double), log2(double);
extern double pow(double, double), sqrt(double), floor(double), ceil(double);
extern double fmod(double, double), ldexp(double, int);
extern double rint(double);
/* Phoenix libm lacks hypot (double) too — provide it (exported for other TUs). */
double hypot(double a, double b) { return sqrt(a * a + b * b); }

float sinf(float x) { return (float)sin((double)x); }
float cosf(float x) { return (float)cos((double)x); }
float tanf(float x) { return (float)tan((double)x); }
float asinf(float x) { return (float)asin((double)x); }
float acosf(float x) { return (float)acos((double)x); }
float atanf(float x) { return (float)atan((double)x); }
float atan2f(float y, float x) { return (float)atan2((double)y, (double)x); }
float expf(float x) { return (float)exp((double)x); }
float logf(float x) { return (float)log((double)x); }
float exp2f(float x) { return (float)exp((double)x * 0.6931471805599453); }
float log2f(float x) { return (float)log2((double)x); }
float powf(float a, float b) { return (float)pow((double)a, (double)b); }
float fmodf(float a, float b) { return (float)fmod((double)a, (double)b); }
float hypotf(float a, float b) { return (float)hypot((double)a, (double)b); }
float ldexpf(float x, int e) { return (float)ldexp((double)x, e); }
float rintf(float x) { return (float)rint((double)x); }
long  lrintf(float x) { return (long)rintf(x); }
float fmaxf(float a, float b) { return a > b ? a : b; }
float fminf(float a, float b) { return a < b ? a : b; }
float copysignf(float x, float y) { return (y < 0.0f) ? -(x < 0.0f ? -x : x) : (x < 0.0f ? -x : x); }
/* NOTE: sqrtf/floorf/ceilf/fabsf/truncf/roundf/lroundf are present in Phoenix
 * libm — do NOT redefine them here (multiple-definition at link). */
