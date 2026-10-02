/* Host stub for libphoenix <arch.h>: the page geometry malloc_dl.c needs, and a
 * counted va2pa(). On the target va2pa() is a system call; here it is msync(),
 * which is also a system call and answers the same "is this page mapped?"
 * question, so the per-pair syscall count matches the target's. */
#ifndef MPH_ARCH_H
#define MPH_ARCH_H

#include <stdint.h>
#include <stddef.h>

#define _PAGE_SHIFT 12U
#define _PAGE_SIZE  (1UL << _PAGE_SHIFT)

extern unsigned long mph_va2paCalls;

static inline uintptr_t va2pa(void *va)
{
	extern int msync(void *addr, size_t length, int flags);
	uintptr_t page = (uintptr_t)va & ~(uintptr_t)(_PAGE_SIZE - 1U);

	__atomic_fetch_add(&mph_va2paCalls, 1UL, __ATOMIC_RELAXED);
	if (msync((void *)page, _PAGE_SIZE, 1 /* MS_ASYNC */) != 0) {
		return 0u;
	}
	return page;
}

#endif
