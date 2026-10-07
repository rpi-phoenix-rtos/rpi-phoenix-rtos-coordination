/* Host stand-in for an <arch.h> WITHOUT hardware math fast paths (no
 * __IEEE754_SQRT, __IEEE754_FLOOR, ...): libm/phoenix then compiles its
 * software fallbacks, which targets lacking those instructions run. */
#ifndef LIBM_HOSTTEST_ARCH_SW_H
#define LIBM_HOSTTEST_ARCH_SW_H
#endif
