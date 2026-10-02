/*
 * The allocator under test as a host object: libphoenix's malloc_dl.c with its
 * public names prefixed phx_, for the Unity tests (see rename.h, Makefile).
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limits.h>
#include <errno.h>
#include <unistd.h>
#include <sysexits.h>
#include <sys/mman.h>

int __libc_multithreaded;
unsigned long mph_va2paCalls;

#define malloc             phx_malloc
#define calloc             phx_calloc
#define realloc            phx_realloc
#define reallocf           phx_reallocf
#define free               phx_free
#define malloc_usable_size phx_malloc_usable_size
#define mallocInfo         phx_mallocInfo
#define malloc_trim        phx_malloc_trim
#define _malloc_init       phx_malloc_init
#define malloc_test        phx_malloc_test

#include MPH_SRC


/* libphoenix runs _malloc_init() before main() */
__attribute__((constructor)) static void alloc_init(void)
{
	phx_malloc_init();
}
