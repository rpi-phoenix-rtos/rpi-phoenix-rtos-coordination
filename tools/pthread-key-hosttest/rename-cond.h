/* Force-included, after rename.h, into the condition-variable suite: its
 * mutexes, condition variables and clocks are libphoenix's too. The types
 * have libphoenix's layout (sys/types.h), and the clock ids are Phoenix's
 * (time.h), which differ from Linux's -- so the suite's clock_gettime() and
 * clock_settime() are the shim's Phoenix clock (shim.c), which a test may
 * step without touching the host's. */
#include "rename.h"

typedef struct {
	unsigned int lock;
	int initialized;
} ph_pthread_mutex_t;

typedef struct {
	unsigned int seq;
	int initialized;
} ph_pthread_cond_t;

typedef struct {
	int pshared;
	int clock_id;
} ph_pthread_condattr_t;

typedef int ph_clockid_t;

int ph_pthread_mutex_init(ph_pthread_mutex_t *mutex, const void *attr);
int ph_pthread_mutex_destroy(ph_pthread_mutex_t *mutex);
int ph_pthread_mutex_lock(ph_pthread_mutex_t *mutex);
int ph_pthread_mutex_trylock(ph_pthread_mutex_t *mutex);
int ph_pthread_mutex_unlock(ph_pthread_mutex_t *mutex);
int ph_pthread_condattr_init(ph_pthread_condattr_t *attr);
int ph_pthread_condattr_destroy(ph_pthread_condattr_t *attr);
int ph_pthread_condattr_setclock(ph_pthread_condattr_t *attr, ph_clockid_t clock);
int ph_pthread_condattr_getclock(const ph_pthread_condattr_t *attr, ph_clockid_t *clock);
int ph_pthread_cond_init(ph_pthread_cond_t *cond, const ph_pthread_condattr_t *attr);
int ph_pthread_cond_destroy(ph_pthread_cond_t *cond);
int ph_pthread_cond_signal(ph_pthread_cond_t *cond);
int ph_pthread_cond_broadcast(ph_pthread_cond_t *cond);
int ph_pthread_cond_wait(ph_pthread_cond_t *cond, ph_pthread_mutex_t *mutex);
int ph_pthread_cond_timedwait(ph_pthread_cond_t *cond, ph_pthread_mutex_t *mutex, const struct timespec *abstime);
int ph_pthread_cond_clockwait(ph_pthread_cond_t *cond, ph_pthread_mutex_t *mutex, ph_clockid_t clock, const struct timespec *abstime);
int shim_clockGettime(ph_clockid_t clock, struct timespec *ts);
int shim_clockSettime(ph_clockid_t clock, const struct timespec *ts);

#define pthread_mutex_t            ph_pthread_mutex_t
#define pthread_cond_t             ph_pthread_cond_t
#define pthread_condattr_t         ph_pthread_condattr_t
#define clockid_t                  ph_clockid_t
#define pthread_mutex_init         ph_pthread_mutex_init
#define pthread_mutex_destroy      ph_pthread_mutex_destroy
#define pthread_mutex_lock         ph_pthread_mutex_lock
#define pthread_mutex_trylock      ph_pthread_mutex_trylock
#define pthread_mutex_unlock       ph_pthread_mutex_unlock
#define pthread_condattr_init      ph_pthread_condattr_init
#define pthread_condattr_destroy   ph_pthread_condattr_destroy
#define pthread_condattr_setclock  ph_pthread_condattr_setclock
#define pthread_condattr_getclock  ph_pthread_condattr_getclock
#define pthread_cond_init          ph_pthread_cond_init
#define pthread_cond_destroy       ph_pthread_cond_destroy
#define pthread_cond_signal        ph_pthread_cond_signal
#define pthread_cond_broadcast     ph_pthread_cond_broadcast
#define pthread_cond_wait          ph_pthread_cond_wait
#define pthread_cond_timedwait     ph_pthread_cond_timedwait
#define pthread_cond_clockwait     ph_pthread_cond_clockwait
#define clock_gettime              shim_clockGettime
#define clock_settime              shim_clockSettime

#undef PTHREAD_COND_INITIALIZER
#define PTHREAD_COND_INITIALIZER { 0, 0 }

#undef CLOCK_MONOTONIC
#undef CLOCK_MONOTONIC_RAW
#undef CLOCK_REALTIME
#undef CLOCK_THREAD_CPUTIME_ID
#define CLOCK_MONOTONIC         0
#define CLOCK_MONOTONIC_RAW     1
#define CLOCK_REALTIME          2
#define CLOCK_THREAD_CPUTIME_ID 3
