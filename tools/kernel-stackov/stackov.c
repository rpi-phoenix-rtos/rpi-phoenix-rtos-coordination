/* Deliberate main-thread stack overflow: infinite recursion with a per-frame
 * buffer. Expected: SIGSEGV when the fixed user stack is exhausted. Tests the
 * kernel signal-delivery-on-exhausted-stack path (double-fault bug). */
#include <stdio.h>
#include <unistd.h>

volatile unsigned long g_sink;

static unsigned long recurse(unsigned long depth)
{
	volatile char buf[4096];
	unsigned i;
	for (i = 0; i < sizeof(buf); i += 512) {
		buf[i] = (char)(depth + i);
	}
	g_sink = buf[0] + buf[512];
	return depth + recurse(depth + 1);
}

int main(void)
{
	printf("stackov: start, recursing to overflow the stack...\n");
	fflush(stdout);
	g_sink = recurse(0);
	printf("stackov: UNEXPECTED return (no overflow?)\n");
	return 0;
}
