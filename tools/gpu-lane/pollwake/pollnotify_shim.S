/*
 * pollNotify() syscall stub for binaries linked against a libphoenix.a that
 * was built before the kernel had the call (aarch64).
 *
 * libphoenix generates one stub per entry of <phoenix/syscalls.h>
 * (arch/aarch64/syscalls.S), numbering them with __COUNTER__ in table order.
 * This file does the same against the KERNEL tree that will run the binary
 * (PW_SYSCALLS_H, set by build.sh), and keeps only pollNotify. build.sh links
 * it only when the sysroot's libphoenix.a has no pollNotify of its own; drop
 * it once the sysroot ships the stub.
 *
 * Copyright 2026 Phoenix Systems
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define __ASSEMBLY__
#include PW_SYSCALLS_H

#define PW_SYSNO(name) .equ pw_sysno_##name, __COUNTER__;
SYSCALLS(PW_SYSNO)

.text

.globl pollNotify
.type pollNotify, %function
pollNotify:
	.cfi_startproc
	stp x0, x1, [sp, #-0x40]!
	stp x2, x3, [sp, #0x10]
	stp x4, x5, [sp, #0x20]
	stp x6, x7, [sp, #0x30]
	.cfi_adjust_cfa_offset 64
	svc pw_sysno_pollNotify
	add sp, sp, #0x40
	.cfi_adjust_cfa_offset -64
	ret
	.cfi_endproc
.size pollNotify, .-pollNotify
