/* Host stand-in for libphoenix <sys/threads.h>: handles map to pthread
 * mutexes; PH_LOCK_RECURSIVE is honoured (FILE locks are recursive). */
#ifndef SH_SYS_THREADS_H
#define SH_SYS_THREADS_H
#include <pthread.h>
#include <stdlib.h>

typedef int handle_t;

#define PH_LOCK_NORMAL     0
#define PH_LOCK_RECURSIVE  1
#define PH_LOCK_ERRORCHECK 2
#define PH_LOCK_PROTO_NOINHERIT 0
#define PH_LOCK_PROTO_INHERIT   1
#define PH_LOCK_STALLED 0
#define PH_LOCK_ROBUST  1

struct lockAttr {
	int type;
	int protocol;
	int robust;
};

#define SH_MUTEX_MAX 65536
extern pthread_mutex_t sh_mutexes[SH_MUTEX_MAX];
extern int sh_mutexCount;

static inline int mutexCreateWithAttr(handle_t *h, const struct lockAttr *attr)
{
	pthread_mutexattr_t a;

	if (sh_mutexCount >= SH_MUTEX_MAX) {
		abort(); /* out of mutex slots */
	}
	pthread_mutexattr_init(&a);
	if ((attr != NULL) && (attr->type == PH_LOCK_RECURSIVE)) {
		pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
	}
	pthread_mutex_init(&sh_mutexes[sh_mutexCount], &a);
	*h = ++sh_mutexCount;
	return 0;
}

static inline int mutexCreate(handle_t *h)
{
	return mutexCreateWithAttr(h, NULL);
}

static inline int mutexLock(handle_t h)
{
	return pthread_mutex_lock(&sh_mutexes[h - 1]);
}

static inline int mutexTry(handle_t h)
{
	return (pthread_mutex_trylock(&sh_mutexes[h - 1]) == 0) ? 0 : -1;
}

static inline int mutexUnlock(handle_t h)
{
	return pthread_mutex_unlock(&sh_mutexes[h - 1]);
}

static inline int resourceDestroy(handle_t h)
{
	(void)h; /* slots are not recycled; a harness run opens few streams */
	return 0;
}
#endif
