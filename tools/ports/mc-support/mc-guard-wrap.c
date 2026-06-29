/*
 * mc-guard-wrap.c — redzone / canary allocator shim for the Midnight Commander
 * (mc 4.8.31) startup heap-OVERFLOW triage (task #55).
 *
 * Linked into mc-guard (MC_VARIANT=guard) with:
 *   -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free
 *
 * BACKGROUND (proven on HW — see GLIB2-MC-PORT-NOTES.md): mc Data-Aborts at
 * startup inside libphoenix dlmalloc's tree walk (malloc_chunkSize) with an
 * ASCII fault address. That is the SURFACE: a write past one allocation has
 * clobbered the next chunk's malloc metadata. The corruption is planted in mc's
 * codeset-INDEPENDENT early init and trips at the malloc burst in mc_args_parse.
 * mc-dbg's markers localize the TRIP site, not the PLANT site; mc-ascii proves
 * it is not the UTF-8 strutil path. We need to catch the WRITER.
 *
 * STRATEGY: every guarded allocation is over-allocated and laid out as
 *
 *   [ guard_hdr_t | LEAD redzone (0xAB) | <user region> | TRAIL redzone (0xAB) ]
 *   ^base (what __real_* sees)           ^user ptr (what mc sees)
 *
 * The header (immediately before the user pointer) records the requested size,
 * a magic, and the allocating backtrace (__builtin_return_address 0..3). On
 * EVERY wrapper entry we scan the canaries of all live guarded blocks BEFORE
 * delegating to the real allocator (so we catch the overflow before libphoenix
 * faults on its own corrupted metadata). When a redzone is clobbered we print
 * ONE decisive line naming the ALLOCATING backtrace + the clobber bytes (hex AND
 * ASCII — the original fault address was ASCII help text, so the clobber content
 * is likely to name the source string), fflush, and _exit.
 *
 * Diagnostic-only. Remove with the rest of the #55 guard scaffolding once the
 * overflowing allocation is fixed.
 *
 * Copyright 2026 Phoenix Systems
 * Author: Witold Bołt
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

extern void *__real_malloc(size_t size);
extern void *__real_calloc(size_t nmemb, size_t size);
extern void *__real_realloc(void *ptr, size_t size);
extern void __real_free(void *ptr);

/* Redzone sizes. Generous trailing zone so a contiguous string overflow is
 * absorbed into our canary (and detected) before it can reach real malloc
 * metadata, even if the scan timing is slightly off. */
#define LEAD_REDZONE 32u
#define TRAIL_REDZONE 64u
#define CANARY_BYTE 0xABu

#define GUARD_MAGIC 0x4D43475544524430ULL /* "MCGUDR D0"-ish; arbitrary 64-bit tag */

typedef struct guard_hdr {
	uint64_t magic;            /* GUARD_MAGIC when this is one of our blocks */
	size_t req;                /* requested user size */
	struct guard_hdr *next;    /* intrusive live-block list */
	struct guard_hdr *prev;
	void *bt[4];               /* allocating return-address chain */
} guard_hdr_t;

/* User region must stay 16-byte aligned. PREFIX = header + lead redzone, rounded
 * up to 16 so header recovery (hdr = (guard_hdr_t*)user - 1 won't hold; we use a
 * fixed PREFIX instead) is O(1) and alignment holds. The header sits at
 * base+PREFIX-sizeof(hdr) — i.e. immediately before the lead redzone is NOT
 * where we put it; we put the header FIRST, then the lead redzone, so the lead
 * redzone is adjacent to the user region. Recover header via user - PREFIX. */
#define ALIGN16(n) (((n) + 15u) & ~((size_t)15u))
#define PREFIX (ALIGN16(sizeof(guard_hdr_t) + LEAD_REDZONE))

static guard_hdr_t *live_head = NULL;
static int in_guard = 0;     /* reentrancy guard: scan/fprintf may re-enter alloc */
static int reporting = 0;    /* once a report starts, never scan again */

static guard_hdr_t *hdr_of(void *user)
{
	return (guard_hdr_t *)((unsigned char *)user - PREFIX);
}

static unsigned char *lead_of(guard_hdr_t *h)
{
	/* lead redzone occupies [base+sizeof(hdr) .. base+PREFIX) i.e. the bytes
	 * immediately before the user region */
	return (unsigned char *)h + PREFIX - LEAD_REDZONE;
}

static unsigned char *trail_of(guard_hdr_t *h)
{
	return (unsigned char *)h + PREFIX + h->req;
}

static int canary_ok(const unsigned char *p, size_t n)
{
	size_t i;
	for (i = 0; i < n; i++) {
		if (p[i] != (unsigned char)CANARY_BYTE)
			return 0;
	}
	return 1;
}

/* Emit the one decisive line for a clobbered block, then _exit. */
static void report_overflow(guard_hdr_t *h, const char *which)
{
	unsigned char *trail = trail_of(h);
	unsigned char *lead = lead_of(h);
	size_t i, over = 0;
	unsigned char ascii[TRAIL_REDZONE + 1];

	reporting = 1;

	/* estimate the overflow length from the count of clobbered redzone bytes */
	if (strcmp(which, "TRAIL") == 0) {
		for (i = 0; i < TRAIL_REDZONE; i++) {
			if (trail[i] != (unsigned char)CANARY_BYTE)
				over = i + 1;
		}
	}
	else {
		for (i = 0; i < LEAD_REDZONE; i++) {
			if (lead[i] != (unsigned char)CANARY_BYTE)
				over++;
		}
	}

	fprintf(stderr,
		"GUARD-OVERFLOW: %s redzone clobbered; block alloc'd size=%zu "
		"by ~%zu bytes\n",
		which, h->req, over);
	fprintf(stderr, "GUARD-ALLOC-BT: %p %p %p %p\n",
		h->bt[0], h->bt[1], h->bt[2], h->bt[3]);

	/* dump the clobbered redzone as hex AND ascii: the original fault address
	 * was ASCII help text, so the clobber content likely names the source
	 * buffer/string directly. */
	{
		unsigned char *z = (strcmp(which, "TRAIL") == 0) ? trail : lead;
		size_t zn = (strcmp(which, "TRAIL") == 0) ? TRAIL_REDZONE : LEAD_REDZONE;
		fprintf(stderr, "GUARD-CLOBBER-HEX:");
		for (i = 0; i < zn; i++)
			fprintf(stderr, " %02x", z[i]);
		fprintf(stderr, "\n");
		for (i = 0; i < zn; i++)
			ascii[i] = (z[i] >= 0x20 && z[i] < 0x7f) ? z[i] : '.';
		ascii[zn] = '\0';
		fprintf(stderr, "GUARD-CLOBBER-ASCII: \"%s\"\n", (char *)ascii);
	}

	fflush(stderr);
	fflush(stdout);
	_exit(55);
}

/* Scan every live guarded block's lead+trail canaries. Called at the TOP of
 * each wrapper, before delegating to the real allocator (libphoenix would
 * otherwise fault on its own corrupted metadata first). */
static void scan_all(void)
{
	guard_hdr_t *h;

	if (reporting)
		return;

	for (h = live_head; h != NULL; h = h->next) {
		if (h->magic != GUARD_MAGIC) {
			/* our own list is corrupted — the overflow hit a header. Report
			 * with whatever backtrace remains; still useful. */
			fprintf(stderr, "GUARD-OVERFLOW: live-list header magic clobbered "
				"(overflow reached an adjacent block header)\n");
			fprintf(stderr, "GUARD-ALLOC-BT: <header destroyed>\n");
			fflush(stderr);
			_exit(55);
		}
		if (!canary_ok(trail_of(h), TRAIL_REDZONE))
			report_overflow(h, "TRAIL");
		if (!canary_ok(lead_of(h), LEAD_REDZONE))
			report_overflow(h, "LEAD");
	}
}

static void list_add(guard_hdr_t *h)
{
	h->prev = NULL;
	h->next = live_head;
	if (live_head != NULL)
		live_head->prev = h;
	live_head = h;
}

static void list_remove(guard_hdr_t *h)
{
	if (h->prev != NULL)
		h->prev->next = h->next;
	else
		live_head = h->next;
	if (h->next != NULL)
		h->next->prev = h->prev;
}

/* Carve a guarded block out of a raw base allocation of total bytes. */
static void *guard_make(void *base, size_t req, void *r0, void *r1, void *r2, void *r3)
{
	guard_hdr_t *h = (guard_hdr_t *)base;
	unsigned char *user;

	h->magic = GUARD_MAGIC;
	h->req = req;
	h->bt[0] = r0;
	h->bt[1] = r1;
	h->bt[2] = r2;
	h->bt[3] = r3;
	list_add(h);

	user = (unsigned char *)base + PREFIX;
	memset(lead_of(h), CANARY_BYTE, LEAD_REDZONE);
	memset(user + req, CANARY_BYTE, TRAIL_REDZONE);
	return user;
}

static size_t guard_total(size_t req)
{
	return PREFIX + req + TRAIL_REDZONE;
}

void *__wrap_malloc(size_t size)
{
	void *base;
	void *r0, *r1, *r2, *r3;

	if (in_guard)
		return __real_malloc(size);

	in_guard = 1;
	scan_all();
	r0 = __builtin_return_address(0);
	r1 = __builtin_return_address(1);
	r2 = __builtin_return_address(2);
	r3 = __builtin_return_address(3);
	base = __real_malloc(guard_total(size));
	if (base == NULL) {
		in_guard = 0;
		return NULL;
	}
	{
		void *u = guard_make(base, size, r0, r1, r2, r3);
		in_guard = 0;
		return u;
	}
}

void *__wrap_calloc(size_t nmemb, size_t size)
{
	size_t req = nmemb * size;
	void *base;
	void *r0, *r1, *r2, *r3;

	if (in_guard) {
		/* calloc must zero; use real calloc to keep semantics */
		return __real_calloc(nmemb, size);
	}

	in_guard = 1;
	scan_all();
	r0 = __builtin_return_address(0);
	r1 = __builtin_return_address(1);
	r2 = __builtin_return_address(2);
	r3 = __builtin_return_address(3);
	/* over-allocate via malloc, then zero only the user region (redzones get
	 * the canary, header gets set by guard_make). */
	base = __real_malloc(guard_total(req));
	if (base == NULL) {
		in_guard = 0;
		return NULL;
	}
	{
		void *u = guard_make(base, req, r0, r1, r2, r3);
		memset(u, 0, req);
		in_guard = 0;
		return u;
	}
}

void *__wrap_realloc(void *ptr, size_t size)
{
	guard_hdr_t *h;
	void *base, *newbase, *u;
	void *r0, *r1, *r2, *r3;
	size_t copy;

	if (in_guard)
		return __real_realloc(ptr, size);

	if (ptr == NULL)
		return __wrap_malloc(size);

	in_guard = 1;
	scan_all();

	h = hdr_of(ptr);
	if (h->magic != GUARD_MAGIC) {
		/* foreign pointer (allocated outside our wrap): delegate untranslated */
		u = __real_realloc(ptr, size);
		in_guard = 0;
		return u;
	}

	r0 = __builtin_return_address(0);
	r1 = __builtin_return_address(1);
	r2 = __builtin_return_address(2);
	r3 = __builtin_return_address(3);

	if (size == 0) {
		/* realloc(ptr,0): free + return NULL (libphoenix semantics) */
		list_remove(h);
		h->magic = 0;
		__real_free((void *)h);
		in_guard = 0;
		return NULL;
	}

	/* Re-carve: allocate a fresh guarded block and copy. Simpler + safer than
	 * in-place realloc since our layout/redzones must be rebuilt anyway. */
	copy = (h->req < size) ? h->req : size;
	newbase = __real_malloc(guard_total(size));
	if (newbase == NULL) {
		in_guard = 0;
		return NULL; /* old block left intact, per realloc contract */
	}
	u = guard_make(newbase, size, r0, r1, r2, r3);
	memcpy(u, ptr, copy);

	/* free the old block */
	list_remove(h);
	base = (void *)h;
	h->magic = 0;
	__real_free(base);

	in_guard = 0;
	return u;
}

void __wrap_free(void *ptr)
{
	guard_hdr_t *h;

	if (ptr == NULL) {
		__real_free(ptr);
		return;
	}

	if (in_guard) {
		__real_free(ptr);
		return;
	}

	in_guard = 1;
	scan_all();

	h = hdr_of(ptr);
	if (h->magic != GUARD_MAGIC) {
		/* foreign pointer: delegate untranslated */
		__real_free(ptr);
		in_guard = 0;
		return;
	}

	/* check this block's own canaries explicitly (scan_all already did, but be
	 * decisive at the free of the very block) */
	if (!canary_ok(trail_of(h), TRAIL_REDZONE))
		report_overflow(h, "TRAIL");
	if (!canary_ok(lead_of(h), LEAD_REDZONE))
		report_overflow(h, "LEAD");

	list_remove(h);
	h->magic = 0; /* poison so a double-free is a foreign pointer next time */
	__real_free((void *)h);
	in_guard = 0;
}
