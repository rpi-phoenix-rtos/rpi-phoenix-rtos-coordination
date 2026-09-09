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

#endif
