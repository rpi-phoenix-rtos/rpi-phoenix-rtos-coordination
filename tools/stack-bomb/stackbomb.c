/*
 * stackbomb.c - deliberately overflow the main-thread user stack to characterize
 * the blast radius of the hal_cpuPushSignal double-fault (kernel signal-frame push
 * writes to the user stack unconditionally; when the stack is exhausted the write
 * itself faults at EL1). Since SIZE_USTACK is now 1 MiB, we recurse ~4 KiB/frame so
 * ~256 frames exhaust it. -O0 + volatile + post-recursion use defeat tail-call opt.
 *
 * Expected: the process must be TERMINATED cleanly (EL0 fault -> SIGSEGV default).
 * The bug under test: whether the kernel additionally double-faults at EL1 while
 * pushing the signal frame, and if that leaves psh/the box alive (cosmetic dump
 * corruption) or panics/hangs it (a userspace-triggerable DoS).
 *
 * Copyright 2026 Phoenix Systems  %LICENSE%
 */
#include <stdio.h>
#include <unistd.h>

volatile unsigned long g_sink;

static unsigned long recurse(unsigned long depth)
{
	volatile char frame[4096];
	frame[0] = (char)depth;
	frame[4095] = (char)(depth >> 8);
	g_sink = (unsigned long)frame[0] + (unsigned long)frame[4095];
	/* recurse BEFORE the final use so the compiler cannot tail-call */
	g_sink += recurse(depth + 1U);
	return g_sink + (unsigned long)frame[0];
}

int main(void)
{
	printf("stackbomb: pid up; recursing 4KiB/frame to overflow the 1MiB user stack\n");
	fflush(stdout);
	g_sink = recurse(0U);
	printf("stackbomb: recurse returned depth-sink=%lu (UNEXPECTED - no overflow)\n", g_sink);
	fflush(stdout);
	return 0;
}
