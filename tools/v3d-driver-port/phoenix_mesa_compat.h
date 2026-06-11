/*
 * phoenix_mesa_compat.h — libc/libm gap shim for cross-compiling Mesa's v3d
 * driver subset to aarch64-phoenix (GLQuake Path C). Phoenix's libm lacks several
 * C99 math functions Mesa uses; provide self-contained inline implementations.
 * Force-include this (-include) ahead of Mesa sources. Grows as the cross-compile
 * gap-enumeration surfaces more missing symbols.
 */
#ifndef PHOENIX_MESA_COMPAT_H
#define PHOENIX_MESA_COMPAT_H
#include <stdint.h>

/* --- C99 math gaps (Phoenix math.h does not declare these) --- */
static inline float  pmc_rintf(float f){ return (float)(f<0.0f?-(long long)(0.5f-f):(long long)(f+0.5f)); }
static inline float  rintf(float f){ return (float)(f<0.0f?-(long long)(0.5f-f):(long long)(f+0.5f)); }
static inline double rint(double d){ return (double)(d<0.0?-(long long)(0.5-d):(long long)(d+0.5)); }
static inline long   lrintf(float f){ return (long)(f<0.0f?-(long long)(0.5f-f):(long long)(f+0.5f)); }
static inline long   lrint(double d){ return (long)(d<0.0?-(long long)(0.5-d):(long long)(d+0.5)); }
static inline long long llroundf(float f){ return (long long)(f<0.0f?f-0.5f:f+0.5f); }
static inline long long llround(double d){ return (long long)(d<0.0?d-0.5:d+0.5); }
static inline long long llrint(double d){ return (long long)(d<0.0?-(long long)(0.5-d):(long long)(d+0.5)); }
static inline long      lround(double d){ return (long)(d<0.0?d-0.5:d+0.5); }
static inline float  roundf(float f){ return (float)(f<0.0f?(long long)(f-0.5f):(long long)(f+0.5f)); }
static inline double round(double d){ return (double)(d<0.0?(long long)(d-0.5):(long long)(d+0.5)); }
static inline float  fmaxf(float a,float b){ return a>b?a:b; }
static inline float  fminf(float a,float b){ return a<b?a:b; }
static inline double fmax(double a,double b){ return a>b?a:b; }
static inline double fmin(double a,double b){ return a<b?a:b; }
static inline float  truncf(float f){ return (float)(long long)f; }


/* --- libc/POSIX gaps --- */
#include <stddef.h>
/* inttypes.h pointer scanf-format macros missing on Phoenix */
#ifndef SCNxPTR
#define SCNxPTR "lx"
#endif
#ifndef SCNuPTR
#define SCNuPTR "lu"
#endif
#ifndef static_assert
#define static_assert _Static_assert
#endif
int posix_memalign(void **memptr, size_t alignment, size_t size);
/* GNU qsort_r (Phoenix has only plain qsort). Real impl is a port task. */
void qsort_r(void *base, size_t nmemb, size_t size,
             int (*compar)(const void *, const void *, void *), void *arg);
/* Phoenix pthread lacks barriers; provide types + decls (impl is a port task). */
typedef struct { int __dummy; } pthread_barrier_t;
typedef struct { int __dummy; } pthread_barrierattr_t;
int pthread_barrier_init(pthread_barrier_t *, const pthread_barrierattr_t *, unsigned);
int pthread_barrier_wait(pthread_barrier_t *);
int pthread_barrier_destroy(pthread_barrier_t *);

#endif /* PHOENIX_MESA_COMPAT_H */
