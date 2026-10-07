/* Host stand-ins for what vm/page.c uses of the kernel (aarch64 flavour: 4 KB pages). */
#ifndef KSTUB_H
#define KSTUB_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EOK 0
#ifndef ENOMEM
#define ENOMEM 12
#define EINVAL 22
#endif

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef uintptr_t ptr_t;
typedef uint64_t addr_t;

#define SIZE_PAGE 0x1000UL

#define PAGE_FREE 0x00000001U
#define PAGE_OWNER_BOOT   (0U << 1)
#define PAGE_OWNER_KERNEL (1U << 1)
#define PAGE_OWNER_APP    (2U << 1)
#define PAGE_KERNEL_SYSPAGE (1U << 4)
#define PAGE_KERNEL_CPU     (2U << 4)
#define PAGE_KERNEL_PTABLE  (3U << 4)
#define PAGE_KERNEL_PMAP    (4U << 4)
#define PAGE_KERNEL_STACK   (5U << 4)
#define PAGE_KERNEL_HEAP    (6U << 4)

#define PGHD_PRESENT 0x20U
#define PGHD_USER    0x08U
#define PGHD_WRITE   0x04U
#define PGHD_READ    0x00U

typedef struct _page_t {
	addr_t addr;
	u8 idx;
	u8 flags;
	struct _page_t *next;
	struct _page_t *prev;
} page_t;

typedef struct { int dummy; } pmap_t;
typedef struct { int dummy; } lock_t;
typedef int lockAttr_t;
static const lockAttr_t proc_lockAttrDefault = 0;
#define proc_lockInit(l, a, n) (0)
#define proc_lockSet(l) (0)
#define proc_lockClear(l) (0)

#define lib_printf printf
#define lib_sprintf sprintf
#define hal_cpuDisableInterrupts() do { } while (0)
#define hal_cpuEnableInterrupts() do { } while (0)

#ifndef min
#define min(a, b) ({ __typeof__(a) _a = (a); __typeof__(b) _b = (b); _a > _b ? _b : _a; })
#define max(a, b) ({ __typeof__(a) _a = (a); __typeof__(b) _b = (b); _a > _b ? _a : _b; })
#endif

/* lib/list.c semantics: add at the tail (before the head), remove moves the head on */
#define LIST_ADD(list, t) do { \
	if (*(list) == NULL) { (t)->next = (t); (t)->prev = (t); *(list) = (t); } \
	else { (t)->prev = (*(list))->prev; (*(list))->prev->next = (t); (t)->next = *(list); (*(list))->prev = (t); } \
} while (0)
#define LIST_REMOVE(list, t) do { \
	if ((t)->next == (t) && (t)->prev == (t)) { *(list) = NULL; } \
	else { (t)->prev->next = (t)->next; (t)->next->prev = (t)->prev; if ((t) == *(list)) *(list) = (t)->next; } \
	(t)->next = NULL; (t)->prev = NULL; \
} while (0)

static inline unsigned int hal_cpuGetLastBit(unsigned long v) { return 63U - (unsigned int)__builtin_clzl(v); }
static inline unsigned int hal_cpuGetFirstBit(unsigned long v) { return (v == 0UL) ? 64U : (unsigned int)__builtin_ctzl(v); }
static inline void *hal_memset(void *d, int c, size_t n) { return memset(d, c, n); }
static inline void *hal_memcpy(void *d, const void *s, size_t n) { return memcpy(d, s, n); }

static inline void *lib_bsearch(void *key, void *base, size_t nmemb, size_t size, int (*compar)(void *n1, void *n2))
{
	size_t lo = 0, hi = nmemb;
	while (lo < hi) {
		size_t mid = lo + (hi - lo) / 2;
		void *it = (char *)base + mid * size;
		int c = compar(key, it);
		if (c == 0) {
			return it;
		}
		if (c > 0) {
			lo = mid + 1;
		}
		else {
			hi = mid;
		}
	}
	return NULL;
}

/* Provided by the simulator */
int pmap_getPage(page_t *page, addr_t *addr);
char pmap_marker(page_t *p);
static inline int pmap_enter(pmap_t *pmap, addr_t pa, void *va, unsigned int attr, page_t *alloc) { (void)pmap; (void)pa; (void)va; (void)attr; (void)alloc; return 0; }
static inline int _pmap_kernelSpaceExpand(pmap_t *pmap, void **start, void *end, page_t *dp) { (void)pmap; (void)start; (void)end; (void)dp; return 0; }

/* proc / vm odds and ends */
typedef struct { void *process; } thread_t_stub;
static inline thread_t_stub *proc_current(void) { static thread_t_stub t; return &t; }
static inline int vm_mapBelongs(void *p, void *a, size_t s) { (void)p; (void)a; (void)s; return 0; }
#endif
