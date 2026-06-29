/*
 * xcalc-dbg-wrap.c — allocator tracer for the X11 Xt/Xaw "Double free detected"
 * triage (task #58).
 *
 * Linked into xcalc-dbg with:
 *   -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free
 *
 * Purpose: libphoenix's free() aborts the process with _exit(EX_SOFTWARE) the
 * instant it is handed a chunk whose CHUNK_CUSED bit is clear ("Double free
 * detected"). Host valgrind on the *identical* code path (the "calculator" ->
 * Pixmap conversion failure) is 100% clean, so the doubly-managed pointer is a
 * libphoenix-specific divergence, NOT an upstream Xt/Xaw logic double-free.
 *
 * These wrappers run BEFORE libphoenix free() can abort, so they print the
 * decisive discriminator for the offending pointer:
 *
 *   FREE-TRACE: about to free a pointer that was NEVER returned by
 *               malloc/calloc/realloc  -> libphoenix handed Xt a static /
 *               aliased / non-heap buffer that Xt then XtFree()'d.
 *   FREE-TRACE: about to free a pointer for the SECOND time
 *               -> genuine double management (aliasing) specific to libphoenix.
 *
 * Either way the printed pointer + size + the free ordinal pins the allocation,
 * and the deterministic single-threaded startup sequence lets the matching host
 * run (with backtrace) name the call site.
 *
 * Diagnostic-only. Remove with the rest of the #58 triage scaffolding once the
 * root cause is fixed.
 *
 * Copyright 2026 Phoenix Systems
 * Author: Witold Bołt
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

extern void *__real_malloc(size_t size);
extern void *__real_calloc(size_t nmemb, size_t size);
extern void *__real_realloc(void *ptr, size_t size);
extern void __real_free(void *ptr);

/* Open-addressing set of live heap pointers. Sized generously: xcalc startup is
 * ~2700 allocs / ~1000 live (measured on host). 16384 slots keeps load < 0.2. */
#define SLOTS 16384u
#define SLOTMASK (SLOTS - 1u)

typedef struct {
	void *ptr;   /* NULL = empty, (void*)-1 = tombstone */
	size_t size;
} slot_t;

static slot_t live[SLOTS];
static unsigned long free_ordinal = 0;
static int trace_ready = 0;

/* Ring of recently-freed pointers, so a second free of a once-malloc'd pointer
 * (hypothesis B) is distinguishable from a free of a never-malloc'd / static
 * pointer (hypothesis A). */
#define FREED_RING 4096u
static void *freed_ring[FREED_RING];
static unsigned freed_ring_pos = 0;

static int freed_recently(void *ptr)
{
	unsigned i;
	for (i = 0; i < FREED_RING; i++) {
		if (freed_ring[i] == ptr)
			return 1;
	}
	return 0;
}

static void freed_note(void *ptr)
{
	freed_ring[freed_ring_pos] = ptr;
	freed_ring_pos = (freed_ring_pos + 1u) & (FREED_RING - 1u);
}

#define TOMB ((void *)-1)

static unsigned slot_hash(void *p)
{
	uintptr_t x = (uintptr_t)p;
	x = (x >> 4) ^ (x >> 16);
	return (unsigned)(x & SLOTMASK);
}

/* Returns index of ptr, or -1 if absent. */
static long set_find(void *ptr)
{
	unsigned h = slot_hash(ptr);
	unsigned i;
	for (i = 0; i < SLOTS; i++) {
		void *cur = live[(h + i) & SLOTMASK].ptr;
		if (cur == NULL)
			return -1;
		if (cur == ptr)
			return (long)((h + i) & SLOTMASK);
	}
	return -1;
}

static void set_add(void *ptr, size_t size)
{
	unsigned h = slot_hash(ptr);
	unsigned i;
	if (ptr == NULL)
		return;
	for (i = 0; i < SLOTS; i++) {
		unsigned idx = (h + i) & SLOTMASK;
		if (live[idx].ptr == NULL || live[idx].ptr == TOMB || live[idx].ptr == ptr) {
			live[idx].ptr = ptr;
			live[idx].size = size;
			return;
		}
	}
	/* table full: fail loud rather than silently lose tracking */
	fprintf(stderr, "FREE-TRACE: tracking table full (raise SLOTS)\n");
	fflush(stderr);
}

static void set_remove(long idx)
{
	if (idx >= 0) {
		live[idx].ptr = TOMB;
		live[idx].size = 0;
	}
}

void *__wrap_malloc(size_t size)
{
	void *p = __real_malloc(size);
	trace_ready = 1;
	set_add(p, size);
	return p;
}

void *__wrap_calloc(size_t nmemb, size_t size)
{
	void *p = __real_calloc(nmemb, size);
	trace_ready = 1;
	set_add(p, nmemb * size);
	return p;
}

void *__wrap_realloc(void *ptr, size_t size)
{
	void *p;
	long oldidx = (ptr != NULL) ? set_find(ptr) : -1;

	p = __real_realloc(ptr, size);
	trace_ready = 1;

	if (ptr != NULL && p != NULL) {
		/* the old block was freed by realloc; the (possibly moved) new block
		 * is now live */
		set_remove(oldidx);
		set_add(p, size);
	}
	else if (ptr == NULL && p != NULL) {
		set_add(p, size); /* realloc(NULL,n) == malloc */
	}
	/* realloc(ptr,0) frees ptr and returns NULL on libphoenix; drop the old */
	else if (size == 0 && ptr != NULL) {
		set_remove(oldidx);
	}
	return p;
}

void __wrap_free(void *ptr)
{
	long idx;

	if (ptr == NULL) {
		__real_free(ptr);
		return;
	}

	free_ordinal++;
	idx = set_find(ptr);

	if (!trace_ready) {
		/* extremely early free before any tracked alloc — just pass through */
		__real_free(ptr);
		return;
	}

	if (idx < 0) {
		if (freed_recently(ptr)) {
			/* hypothesis B: this pointer WAS malloc'd and already freed once;
			 * something is freeing it a second time. Genuine double management
			 * (aliasing) that only manifests under libphoenix. */
			fprintf(stderr,
				"FREE-TRACE: free #%lu DOUBLE-FREE ptr=%p "
				"(was malloc'd, already freed once) -> hypothesis B aliasing\n",
				free_ordinal, ptr);
		}
		else {
			/* hypothesis A: this pointer was never returned by
			 * malloc/calloc/realloc, yet something is about to free() it.
			 * A libphoenix libc call handed Xt a static / aliased / non-heap
			 * buffer where glibc returns a fresh malloc'd one, and Xt then
			 * XtFree()'d it. */
			fprintf(stderr,
				"FREE-TRACE: free #%lu NON-HEAP / never-malloc'd ptr=%p "
				"-> static/aliased buffer freed (hypothesis A)\n",
				free_ordinal, ptr);
		}
		fflush(stderr);
		/* pass through unchanged; libphoenix will abort here, but our marker is
		 * already on the wire */
		__real_free(ptr);
		return;
	}

	/* normal single free: remove from live set, record it, then free. Stay
	 * quiet on the common case so we do not flood the UART with ~1000 lines;
	 * only the two smoking-gun cases above print. */
	set_remove(idx);
	freed_note(ptr);
	__real_free(ptr);
}
