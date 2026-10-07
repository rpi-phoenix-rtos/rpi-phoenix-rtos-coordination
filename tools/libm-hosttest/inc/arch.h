/* Host stand-in for libphoenix's <arch.h>, as seen by libm/phoenix on x86-64.
 * It declares the same hardware fast paths the aarch64 arch.h does (fsqrt,
 * frint*, fabs), through gcc builtins that compile to the x86 equivalents, so
 * the host build takes the code paths the Pi takes. Needs -mavx (implied by
 * -mfma) so floor/ceil/trunc are single ROUNDSD instructions rather than calls
 * back into the very functions being defined; round has no x86 instruction. */
#ifndef LIBM_HOSTTEST_ARCH_H
#define LIBM_HOSTTEST_ARCH_H

#define __IEEE754_SQRT
#define __ieee754_sqrt(x) __builtin_sqrt(x)
#define __IEEE754_SQRTF
#define __ieee754_sqrtf(x) __builtin_sqrtf(x)
#define __IEEE754_FABS
#define __ieee754_fabs(x) __builtin_fabs(x)
#define __IEEE754_FABSF
#define __ieee754_fabsf(x) __builtin_fabsf(x)
#define __IEEE754_CEIL
#define __ieee754_ceil(x) __builtin_ceil(x)
#define __IEEE754_CEILF
#define __ieee754_ceilf(x) __builtin_ceilf(x)
#define __IEEE754_FLOOR
#define __ieee754_floor(x) __builtin_floor(x)
#define __IEEE754_FLOORF
#define __ieee754_floorf(x) __builtin_floorf(x)
#define __IEEE754_ROUND
#define __ieee754_round(x) ({ double a_ = (x), t_ = __builtin_trunc(a_); (__builtin_fabs(a_ - t_) >= 0.5) ? t_ + __builtin_copysign(1.0, a_) : t_; })
#define __IEEE754_ROUNDF
#define __ieee754_roundf(x) ({ float a_ = (x), t_ = __builtin_truncf(a_); (__builtin_fabsf(a_ - t_) >= 0.5f) ? t_ + __builtin_copysignf(1.0f, a_) : t_; })
#define __IEEE754_TRUNC
#define __ieee754_trunc(x) __builtin_trunc(x)
#define __IEEE754_TRUNCF
#define __ieee754_truncf(x) __builtin_truncf(x)

#endif
