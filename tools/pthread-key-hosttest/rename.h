/* Force-included into the phoenix-rtos-tests source built for the host: the
 * pthread functions under test go to libphoenix's copy (ph_*), everything else
 * -- barriers, rwlocks, sleeping, stdio -- stays the host's. The host headers
 * come first, so the renames cannot touch their declarations. Phoenix threads
 * run on host threads (shim.c), so the host's synchronisation works across
 * them. */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct __pthread_key_t *ph_pthread_key_t; /* a pointer on Phoenix, unsigned int here */
typedef uintptr_t ph_pthread_t;

int ph_pthread_create(ph_pthread_t *thread, const void *attr, void *(*start)(void *), void *arg);
int ph_pthread_join(ph_pthread_t thread, void **ret);
int ph_pthread_cancel(ph_pthread_t thread);
int ph_pthread_detach(ph_pthread_t thread);
ph_pthread_t ph_pthread_self(void);
int ph_pthread_key_create(ph_pthread_key_t *key, void (*destructor)(void *));
int ph_pthread_key_delete(ph_pthread_key_t key);
int ph_pthread_setspecific(ph_pthread_key_t key, const void *value);
void *ph_pthread_getspecific(ph_pthread_key_t key);

#define pthread_t             ph_pthread_t
#define pthread_key_t         ph_pthread_key_t
#define pthread_create        ph_pthread_create
#define pthread_join          ph_pthread_join
#define pthread_cancel        ph_pthread_cancel
#define pthread_detach        ph_pthread_detach
#define pthread_self          ph_pthread_self
#define pthread_key_create    ph_pthread_key_create
#define pthread_key_delete    ph_pthread_key_delete
#define pthread_setspecific   ph_pthread_setspecific
#define pthread_getspecific   ph_pthread_getspecific

/* Phoenix's value: what pthread_join() reports for a cancelled thread */
#undef PTHREAD_CANCELED
#define PTHREAD_CANCELED ((void *)2)

/* The test's main() becomes test_main(); main.c sets the library up first */
#define main test_main
