/* Host stand-ins for the kernel services uchannel.c, pipe.c and cbuffer.c use. */
#ifndef KSTUB_H
#define KSTUB_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <sys/types.h>

#define EOK 0
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef uint64_t id_t_k;
typedef uintptr_t ptr_t;
typedef uintptr_t addr_t;
typedef long long time_t_k;

typedef struct { u32 port; u64 id; } oid_t;
typedef struct { pthread_mutex_t m; } lock_t;
typedef struct _thread_t thread_t; /* a wait queue: points at a heap pthread_cond_t */
typedef int lockAttr_t;
extern const lockAttr_t proc_lockAttrDefault;

#define MAYBE_UNUSED __attribute__((unused))
#define LIB_ASSERT(c, ...) do { if (!(c)) abort(); } while (0)
#define LIB_ASSERT_ALWAYS(c, ...) do { if (!(c)) abort(); } while (0)
#ifndef min
#define min(a, b) ({ __typeof__(a) _a = (a); __typeof__(b) _b = (b); _a > _b ? _b : _a; })
#define max(a, b) ({ __typeof__(a) _a = (a); __typeof__(b) _b = (b); _a > _b ? _a : _b; })
#endif
#define lib_atomicIncrement(ptr) ((__typeof__(*(ptr)))__atomic_add_fetch((ptr), 1, __ATOMIC_RELAXED))
#define lib_atomicDecrement(ptr) ((__typeof__(*(ptr)))__atomic_sub_fetch((ptr), 1, __ATOMIC_RELAXED))

#define LIST_ADD(list, t) do { \
	if (*(list) == NULL) { (t)->next = (t); (t)->prev = (t); *(list) = (t); } \
	else { (t)->prev = (*(list))->prev; (*(list))->prev->next = (t); (t)->next = *(list); (*(list))->prev = (t); } \
} while (0)
#define LIST_REMOVE(list, t) do { \
	if ((t)->next == (t) && (t)->prev == (t)) { *(list) = NULL; } \
	else { (t)->prev->next = (t)->next; (t)->next->prev = (t)->prev; if ((t) == *(list)) *(list) = (t)->next; } \
	(t)->next = NULL; (t)->prev = NULL; \
} while (0)

static inline void *hal_memcpy(void *d, const void *s, size_t n) { return memcpy(d, s, n); }
static inline void *hal_memset(void *d, int c, size_t n) { return memset(d, c, n); }
static inline unsigned int hal_cpuGetLastBit(unsigned long v) { return 63U - (unsigned int)__builtin_clzl(v); }

void *vm_kmalloc(size_t size);
void vm_kfree(void *p);
extern int kstub_failAlloc; /* make the next vm_kmalloc() fail */
extern size_t kstub_bytes;  /* live bytes */

int proc_lockInit(lock_t *l, const lockAttr_t *a, const char *name);
int proc_lockDone(lock_t *l);
int proc_lockSet(lock_t *l);
int proc_lockClear(lock_t *l);
int proc_lockWait(thread_t **queue, lock_t *l, time_t_k timeout);
int proc_threadBroadcast(thread_t **queue);

/* pollwake */
void pollwake_notify(const oid_t *oid);
void pollwake_notifyUnix(void);
extern unsigned kstub_notifies, kstub_notifiesUnix;

/* fdpass */
typedef struct fdpack_s { struct fdpack_s *next, *prev; } fdpack_t;
void fdpass_discard(fdpack_t **packs);

/* posix */
#define O_RDONLY   0x00001U
#define O_WRONLY   0x00002U
#define O_RDWR     0x00004U
#define O_NONBLOCK 0x01000U
#define POLLIN     0x1U
#define POLLRDNORM 0x2U
#define POLLOUT    0x10U
#define POLLWRNORM 0x20U
#define POLLERR    0x80U
#define POLLHUP    0x100U
#define POSIX_PORT_PIPE 0xfffffffdU
#endif
