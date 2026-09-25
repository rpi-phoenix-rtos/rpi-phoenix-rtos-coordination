/* Host stub for libphoenix <arch.h>.
 * malloc_dl.c only needs _PAGE_SIZE (plus the fixed-width types that the real
 * arch.h drags in through its per-arch stdint header).  aarch64 _PAGE_SHIFT is 12U
 * (sources/phoenix-rtos-kernel/include/arch/aarch64/page.h:17), which matches
 * the host x86-64 page size, so the arena geometry is identical to the target. */
#ifndef HZ_ARCH_H
#define HZ_ARCH_H

#include <stdint.h>
#include <stddef.h>

#define _PAGE_SHIFT 12U
#define _PAGE_SIZE  (1UL << _PAGE_SHIFT)

/* Host stand-in for Phoenix's va2pa() (libphoenix <sys/mman.h>), which the
 * allocator uses to ask "is this page mapped right now?" -- on the target it is
 * the va2pa syscall, i.e. pmap_resolve(), returning 0 for an invalid descriptor.
 *
 * msync() answers the same question on the host: it fails with ENOMEM on a range
 * that is not mapped, and succeeds on a mapped anonymous private one. Crucially
 * it asks the KERNEL, not any bookkeeping of the allocator's or the harness's,
 * so a test built on it is not circular -- which is the whole point, since what
 * is under test is precisely whether malloc's live[] ring agrees with reality.
 *
 * The return value only has to be zero/non-zero to the allocator; it never uses
 * it as an address. Declared here rather than in a <sys/mman.h> stub so the real
 * host header keeps providing mmap/munmap unmolested. */
static inline uintptr_t va2pa(void *va)
{
	uintptr_t page = (uintptr_t)va & ~(uintptr_t)(_PAGE_SIZE - 1U);

	extern int msync(void *addr, size_t length, int flags);
	if (msync((void *)page, _PAGE_SIZE, 1 /* MS_ASYNC */) != 0) {
		return 0u;
	}
	return page;
}

#endif
