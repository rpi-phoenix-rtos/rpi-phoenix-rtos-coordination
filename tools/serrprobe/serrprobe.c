/* P2 E2: provoke a known asynchronous SError to prove the unmasked handler fires.
 * Linux on this board panics with esr=0xbf000002 on ONE read of 0xfd506000
 * (docs/done 2026-05-30 linux-usb-ref capture). Usage: serrprobe [pa_hex] [reads] */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	uintptr_t pa = (argc > 1) ? (uintptr_t)strtoull(argv[1], NULL, 16) : 0xfd506000u;
	int reads = (argc > 2) ? atoi(argv[2]) : 1;
	uintptr_t page = pa & ~(uintptr_t)0xfff;
	volatile uint32_t *base;
	int i;

	base = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
		MAP_DEVICE | MAP_UNCACHED | MAP_PHYSMEM | MAP_ANONYMOUS, -1, (off_t)page);
	if (base == MAP_FAILED) {
		printf("serrprobe: mmap 0x%lx failed\n", (unsigned long)page);
		return 1;
	}
	printf("serrprobe: reading 0x%lx x%d\n", (unsigned long)pa, reads);
	fflush(stdout);
	for (i = 0; i < reads; ++i) {
		uint32_t v = base[(pa - page) / 4u];
		printf("serrprobe: read %d = 0x%08x\n", i, (unsigned)v);
		fflush(stdout);
		usleep(200000);
	}
	printf("serrprobe: done\n");
	return 0;
}
