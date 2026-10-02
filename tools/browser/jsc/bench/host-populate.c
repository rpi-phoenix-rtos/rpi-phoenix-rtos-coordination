/*
 * Host-side footprint emulation for the jsc shell (browser B9): an LD_PRELOAD shim that makes every
 * accessible anonymous mmap() resident at once (MAP_POPULATE), as Phoenix-RTOS does, so that the
 * host's RSS counts reservations the way Phoenix's `--footprint` does. README "JIT footprint".
 *
 *   gcc -O2 -shared -fPIC -o host-populate.so host-populate.c -ldl
 *   ulimit -s 1024     # WTF threads get 1 MiB stacks on Phoenix; glibc uses RLIMIT_STACK
 *   JSC_structureHeapSizeInKB=65536 MIMALLOC_ARENA_RESERVE=32MiB MIMALLOC_PURGE_DELAY=-1 \
 *   WTF_numberOfProcessorCores=4 LD_PRELOAD=$PWD/host-populate.so taskset -c 0-3 \
 *       jsc-host-jit --forceRAMSize=4000000000 --jitMemoryReservationSize=33554432 micro.js
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stddef.h>
#include <sys/mman.h>
#include <sys/types.h>


static void *(*real_mmap)(void *, size_t, int, int, int, off_t);


void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
	if (real_mmap == NULL) {
		real_mmap = (void *(*)(void *, size_t, int, int, int, off_t))dlsym(RTLD_NEXT, "mmap");
	}
	if (((flags & MAP_ANONYMOUS) != 0) && (prot != PROT_NONE)) {
		flags = (flags | MAP_POPULATE) & ~MAP_NORESERVE;
	}
	return real_mmap(addr, len, prot, flags, fd, off);
}


void *mmap64(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
	return mmap(addr, len, prot, flags, fd, off);
}
