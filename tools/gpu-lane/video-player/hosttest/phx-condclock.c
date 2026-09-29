/*
 * Host control for the M10 ffplay quit/EOF hang: an LD_PRELOAD shim that gives the host
 * glibc libphoenix's condition-variable clock default.
 *
 * libphoenix creates an attribute-less condition variable on CLOCK_MONOTONIC
 * (sources/libphoenix/pthread/pthread.c: PTHREAD_COND_CLOCK_DEFAULT, kept on purpose by
 * c283f2d), while SDL 2.30's pthread SDL_CondWaitTimeout() builds its deadline from
 * CLOCK_REALTIME. Once the boot has set the wall clock (1970 -> today), that deadline lies
 * decades ahead on the monotonic clock, so every "10 ms" wait lasts until the next signal.
 *
 *   default build        pthread_cond_init(c, NULL) creates the condvar on CLOCK_MONOTONIC,
 *                        as libphoenix does (SDL itself is the host's, unchanged)
 *   -DWITH_SDL_FIX       also SDL_CondWaitTimeout() as sdl-patches/0011 builds it on Phoenix:
 *                        the deadline on CLOCK_MONOTONIC (0011 also creates SDL's condvars on
 *                        CLOCK_MONOTONIC explicitly, which the default above already gives)
 *
 * Copyright 2026 Phoenix Systems
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>


int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr)
{
	static int (*real)(pthread_cond_t *, const pthread_condattr_t *);
	pthread_condattr_t mono;
	int err;

	if (real == NULL) {
		real = (int (*)(pthread_cond_t *, const pthread_condattr_t *))dlsym(RTLD_NEXT, "pthread_cond_init");
	}
	if (attr != NULL) {
		return real(cond, attr);
	}
	pthread_condattr_init(&mono);
	pthread_condattr_setclock(&mono, CLOCK_MONOTONIC);
	err = real(cond, &mono);
	pthread_condattr_destroy(&mono);
	return err;
}


#ifdef WITH_SDL_FIX

/* SDL 2.30's private layouts (src/thread/pthread/SDL_syscond.c, SDL_sysmutex_c.h) */
struct SDL_cond {
	pthread_cond_t cond;
};

struct SDL_mutex {
	pthread_mutex_t id;
};

#define SDL_MUTEX_TIMEDOUT 1


int SDL_CondWaitTimeout(struct SDL_cond *cond, struct SDL_mutex *mutex, unsigned int ms)
{
	struct timespec abstime;
	int err;

	if (cond == NULL) {
		return -1;
	}
	clock_gettime(CLOCK_MONOTONIC, &abstime);
	abstime.tv_nsec += (ms % 1000) * 1000000;
	abstime.tv_sec += ms / 1000;
	if (abstime.tv_nsec >= 1000000000) {
		abstime.tv_sec += 1;
		abstime.tv_nsec -= 1000000000;
	}
	do {
		err = pthread_cond_timedwait(&cond->cond, &mutex->id, &abstime);
	} while (err == EINTR);
	if (err == ETIMEDOUT) {
		return SDL_MUTEX_TIMEDOUT;
	}
	return (err == 0) ? 0 : -1;
}

#endif
