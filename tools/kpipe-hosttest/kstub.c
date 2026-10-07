/* Host implementations of the kernel services stubbed in stub/kstub.h */
#include "kstub.h"
#include <stdio.h>

const lockAttr_t proc_lockAttrDefault = 0;
int kstub_failAlloc;
size_t kstub_bytes;
unsigned kstub_notifies, kstub_notifiesUnix;

void *vm_kmalloc(size_t size)
{
	size_t *p;
	if (kstub_failAlloc > 0) {
		kstub_failAlloc--;
		return NULL;
	}
	p = malloc(size + 16);
	p[0] = size;
	__atomic_add_fetch(&kstub_bytes, size, __ATOMIC_SEQ_CST);
	return (char *)p + 16;
}

void vm_kfree(void *q)
{
	size_t *p;
	if (q == NULL) {
		return;
	}
	p = (size_t *)((char *)q - 16);
	__atomic_sub_fetch(&kstub_bytes, p[0], __ATOMIC_SEQ_CST);
	free(p);
}

int proc_lockInit(lock_t *l, const lockAttr_t *a, const char *name)
{
	(void)a; (void)name;
	return pthread_mutex_init(&l->m, NULL);
}

int proc_lockDone(lock_t *l) { return pthread_mutex_destroy(&l->m); }
int proc_lockSet(lock_t *l) { return pthread_mutex_lock(&l->m); }
int proc_lockClear(lock_t *l) { return pthread_mutex_unlock(&l->m); }

/* Every caller holds the channel lock when it waits on or broadcasts a queue,
 * so the lazily created condition variable needs no lock of its own. */
static pthread_cond_t *kstub_cond(thread_t **queue)
{
	pthread_cond_t *c = (pthread_cond_t *)*queue;
	if (c == NULL) {
		c = malloc(sizeof(*c));
		pthread_cond_init(c, NULL);
		*queue = (thread_t *)c;
	}
	return c;
}

int proc_lockWait(thread_t **queue, lock_t *l, time_t_k timeout)
{
	(void)timeout;
	pthread_cond_wait(kstub_cond(queue), &l->m);
	return 0;
}

int proc_threadBroadcast(thread_t **queue)
{
	pthread_cond_broadcast(kstub_cond(queue));
	return 0;
}

void pollwake_notify(const oid_t *oid)
{
	(void)oid;
	__atomic_add_fetch(&kstub_notifies, 1, __ATOMIC_RELAXED);
}

void pollwake_notifyUnix(void)
{
	__atomic_add_fetch(&kstub_notifiesUnix, 1, __ATOMIC_RELAXED);
}

void fdpass_discard(fdpack_t **packs)
{
	(void)packs;
	abort(); /* a pipe never carries descriptors */
}
