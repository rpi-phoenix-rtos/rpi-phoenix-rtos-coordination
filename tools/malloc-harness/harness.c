/*
 * Host harness for the Phoenix-RTOS libphoenix Doug-Lea allocator.
 *
 * Compiles the REAL, UNMODIFIED sources/libphoenix/stdlib/malloc_dl.c (plus the
 * real sys/rb.c and sys/list.c) for the host and hunts for the invariant
 * violation suspected behind the SuperTuxKart teardown Data Abort:
 *
 *   _malloc_chunkJoin (malloc_dl.c:346, forward-join loop) faulting on a READ
 *   of chunk->heap->size (malloc_chunkIsLast, :149) at a page-aligned address
 *   -- i.e. a still-reachable chunk pointing at a munmap()ed heap.
 *
 * malloc_dl.c is #included into this translation unit so that malloc_common and
 * the file-static helpers are visible to the checker.  Nothing under sources/ is
 * modified: the Phoenix-only dependencies are satisfied by stubs/ and the public
 * entry points are renamed with macros so they do not collide with host libc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define _GNU_SOURCE

/* Every host header the allocator (or the real sys/rb.h) needs must be pulled in
 * BEFORE the rename block below, otherwise the macros would mangle their
 * declarations. */
#include <pthread.h>
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
#include <setjmp.h>
#include <signal.h>
#include <sys/mman.h>

/* ------------------------------------------------------------------ */
/* Real syscall thunks, captured before mmap/munmap are macro-renamed. */

static void *hz_sys_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
	return mmap(addr, len, prot, flags, fd, off);
}


static int hz_sys_munmap(void *addr, size_t len)
{
	return munmap(addr, len);
}


/* ------------------------------------------------------------------ */
/* Region table: every mmap() the allocator makes is a heap.           */

#define HZ_MAX_REGIONS 8192

typedef struct {
	uintptr_t base;
	size_t size;
	int mapped; /* 1 = live heap, 0 = munmap()ed, entry kept as a tombstone */
	unsigned long unmapSeq;
} hz_region_t;

static hz_region_t hz_regions[HZ_MAX_REGIONS];
static int hz_nregions;
static unsigned long hz_seq;

/* Coverage counters -- a clean run is only meaningful if the workload actually
 * reached the paths under suspicion. */
static struct {
	unsigned long mmaps;
	unsigned long munmaps;
	unsigned long checks;
	unsigned long chunksWalked;
	unsigned long freeChunksSeen;
	unsigned long reallocShrinkSplit;
	unsigned long reallocGrowInPlace;
	unsigned long joinBackward;
	unsigned long joinForward;
	int maxLiveHeaps;
	int maxBinChunks;
	int maxChunksInHeap;
} hz_cov;

static void *hz_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off);
static int hz_munmap(void *addr, size_t len);

/* Violation reporting -------------------------------------------------- */

enum {
	HZ_OK = 0,
	HZ_V_FREEHEAP_MULTICHUNK,  /* invariant 1 */
	HZ_V_BIN_IN_DEAD_HEAP,     /* invariant 2 */
	HZ_V_PTR_INTO_DYING_HEAP,  /* invariant 2b: a bin/tree link into the heap being unmapped */
	HZ_V_ADJACENT_FREE,        /* coalescing hole */
	HZ_V_FREESZ_MISMATCH,
	HZ_V_PUSED_MISMATCH,
	HZ_V_FOOTER_MISMATCH,
	HZ_V_ORPHAN_FREE_CHUNK,    /* free chunk not present in any bin */
	HZ_V_GHOST_BIN_CHUNK,      /* bin chunk that is not a free chunk of a live heap */
	HZ_V_WRONG_BIN,
	HZ_V_BINMAP,
	HZ_V_RB_STRUCTURE,
	HZ_V_HEAP_WALK_CORRUPT,
	HZ_V_DATA_CORRUPT,         /* payload clobbered => overlapping allocations */
	HZ_V_SEGV,
	HZ_V_CANARY,               /* --fragile: not a bug, exercises the shrinker */
	HZ_V_LIVE_RING,            /* live[] names a page that is not mapped */
};

static const char *const hz_vname[] = {
	"OK",
	"INV1: fully-free heap holds more than one chunk",
	"INV2: bin chunk references an unmapped heap",
	"INV2b: bin/tree link points into the heap about to be unmapped",
	"two adjacent free chunks (incomplete coalescing)",
	"heap->freesz != sum of free chunk sizes",
	"CHUNK_PUSED disagrees with the previous chunk's CHUNK_CUSED",
	"free chunk footer != chunk size",
	"free chunk missing from every bin",
	"bin holds a chunk that is not a free chunk of a live heap",
	"free chunk sits in the wrong bin",
	"sbinmap/lbinmap disagrees with bin contents",
	"red-black tree structure invalid",
	"heap chunk walk hit a corrupt header",
	"allocation payload corrupted (overlapping blocks)",
	"SIGSEGV",
	"harness canary (--fragile): heap chunk count exceeded",
	"live[] ring names a heap that is not currently mapped",
};

static int hz_violation;
static char hz_vdetail[512];
static long hz_vop = -1;
static int hz_quiet;

#define HZ_FAIL(code, ...) \
	do { \
		if (hz_violation == HZ_OK) { \
			hz_violation = (code); \
			snprintf(hz_vdetail, sizeof(hz_vdetail), __VA_ARGS__); \
			if (hz_quiet == 0) { \
				fprintf(stderr, "\n*** VIOLATION [%s]\n***   %s\n", hz_vname[code], hz_vdetail); \
			} \
		} \
	} while (0)

/* ------------------------------------------------------------------ */
/* Rename the allocator's public surface, then pull in the real source. */

#define mmap               hz_mmap
#define munmap             hz_munmap
#define malloc             phx_malloc
#define calloc             phx_calloc
#define realloc            phx_realloc
#define reallocf           phx_reallocf
#define free               phx_free
#define malloc_usable_size phx_malloc_usable_size
#define _malloc_init       phx_malloc_init
#define malloc_test        phx_malloc_test

#include "../../sources/libphoenix/stdlib/malloc_dl.c"


/* ------------------------------------------------------------------ */
/* Fake v3d BO-attribution table, so the WIRING can be tested on the host.
 *
 * malloc asks v3d_c1_lookup_pa() on a corrupt-header fire: "was this physical
 * frame a closed buffer object?". On hardware the v3d driver supplies it and the
 * answer only appears when C1 actually fires -- roughly 1 run in 16. Shipping an
 * instrument whose call site has never been executed and then waiting hours for a
 * rare event to discover it was never wired is exactly the trap this project keeps
 * paying for ("check a new detector can fire BEFORE trusting its silence").
 *
 * This definition satisfies the weak symbol, records that it was called and with
 * what, and returns a canned hit. It proves the call site executes and passes the
 * page-aligned physical address -- everything except the driver's own table. */
static int hz_boLookupCalls;
static unsigned long hz_boLookupArg;

int v3d_c1_lookup_pa(unsigned long pa, unsigned int *npages, unsigned int *ord,
	unsigned int *total)
{
	hz_boLookupCalls++;
	hz_boLookupArg = pa;
	if (npages != NULL) *npages = 3u;
	if (ord != NULL) *ord = 42u;
	if (total != NULL) *total = 300u;
	return 1;
}

/* ------------------------------------------------------------------ */
/* Region table implementation (after the include; it calls no allocator). */

static int hz_regionFind(uintptr_t p)
{
	int i;

	for (i = 0; i < hz_nregions; i++) {
		if ((p >= hz_regions[i].base) && (p < hz_regions[i].base + hz_regions[i].size)) {
			return i;
		}
	}

	return -1;
}


static int hz_regionFindBase(uintptr_t base)
{
	int i;

	for (i = 0; i < hz_nregions; i++) {
		if (hz_regions[i].base == base) {
			return i;
		}
	}

	return -1;
}


static void hz_regionDrop(int i)
{
	hz_regions[i] = hz_regions[--hz_nregions];
}


static void hz_regionAdd(uintptr_t base, size_t size)
{
	int i;

	/* Drop tombstones that overlap the fresh mapping so they cannot cause
	 * false positives once the kernel recycles an address range. */
	for (i = 0; i < hz_nregions;) {
		if ((hz_regions[i].mapped == 0) &&
				(hz_regions[i].base < base + size) &&
				(base < hz_regions[i].base + hz_regions[i].size)) {
			hz_regionDrop(i);
		}
		else {
			i++;
		}
	}

	if (hz_nregions == HZ_MAX_REGIONS) {
		for (i = 0; i < hz_nregions; i++) {
			if (hz_regions[i].mapped == 0) {
				hz_regionDrop(i);
				break;
			}
		}
	}

	if (hz_nregions == HZ_MAX_REGIONS) {
		fprintf(stderr, "harness: region table full of live heaps\n");
		exit(2);
	}

	hz_regions[hz_nregions].base = base;
	hz_regions[hz_nregions].size = size;
	hz_regions[hz_nregions].mapped = 1;
	hz_regions[hz_nregions].unmapSeq = 0;
	hz_nregions++;
}


/* 1 if p..p+len-1 lies inside a heap that is currently mapped. */
static int hz_mappedRange(const void *p, size_t len)
{
	int i = hz_regionFind((uintptr_t)p);

	if (i < 0) {
		return 0;
	}
	if (hz_regions[i].mapped == 0) {
		return 0;
	}

	return ((uintptr_t)p + len) <= (hz_regions[i].base + hz_regions[i].size);
}


/* Why a chunk pointer failed the range test -- an unmapped heap is the crash
 * signature we are hunting; an unknown address means corruption. */
static const char *hz_whyBad(const void *p)
{
	int i = hz_regionFind((uintptr_t)p);

	if (i < 0) {
		return "address belongs to no heap the allocator ever mmap()ed";
	}
	if (hz_regions[i].mapped == 0) {
		return "address is inside a heap that has already been munmap()ed";
	}

	return "header would run past the end of its heap";
}


/* ------------------------------------------------------------------ */
/* Replicated chunk geometry (deliberately NOT the file's own inlines).  */

static int hz_fragile;

#define HZ_HEAPHDR sizeof(heap_t)

static size_t hz_chunkSize(const chunk_t *c)
{
	return c->size & ~(size_t)(CHUNK_CUSED | CHUNK_PUSED);
}


static int hz_chunkIsLast(const chunk_t *c)
{
	return ((uintptr_t)c + hz_chunkSize(c) + CHUNK_MIN_SIZE) > ((uintptr_t)c->heap + c->heap->size);
}


static int hz_chunkIsFree(const chunk_t *c)
{
	return (c->size & CHUNK_CUSED) == 0;
}


/* ------------------------------------------------------------------ */
/* Bin snapshot.                                                        */

#define HZ_MAX_BINCHUNKS 262144

/* How a binned chunk is held.  This matters: _malloc_chunkAdd():264-268 only
 * initialises the rbnode of the chunk it actually inserts into the tree; a
 * duplicate-size chunk gets node.parent = &node as a "not in the tree" marker and
 * its node.left/node.right are left as STALE GARBAGE.  Reading those as pointers
 * produces false positives, so the kind has to be tracked. */
enum { HZ_K_SBIN = 0, HZ_K_TREE, HZ_K_LISTED };

static chunk_t *hz_bin[HZ_MAX_BINCHUNKS];
static int hz_binIdx[HZ_MAX_BINCHUNKS];
static int hz_binKind[HZ_MAX_BINCHUNKS];
static int hz_nbin;
static int hz_binOverflow;

static void hz_binPush(chunk_t *c, int idx, int kind)
{
	if (hz_nbin < HZ_MAX_BINCHUNKS) {
		hz_bin[hz_nbin] = c;
		hz_binIdx[hz_nbin] = idx;
		hz_binKind[hz_nbin] = kind;
		hz_nbin++;
	}
	else {
		hz_binOverflow = 1;
	}
}


static int hz_binSeen(const chunk_t *c)
{
	int i;

	for (i = 0; i < hz_nbin; i++) {
		if (hz_bin[i] == c) {
			return 1;
		}
	}

	return 0;
}


/* Walk one circular next/prev list, pushing every member. `head` is included. */
static int hz_collectList(chunk_t *head, int idx, int isLarge)
{
	chunk_t *c = head;
	int guard = 0;
	int kind;

	do {
		if (hz_mappedRange(c, CHUNK_MIN_SIZE) == 0) {
			HZ_FAIL(HZ_V_BIN_IN_DEAD_HEAP,
					"%s bin %d: list member %p unusable -- %s",
					isLarge ? "lbin" : "sbin", idx, (void *)c, hz_whyBad(c));
			return -1;
		}
		if (hz_binSeen(c) != 0) {
			HZ_FAIL(HZ_V_RB_STRUCTURE, "%s bin %d: chunk %p reachable twice",
					isLarge ? "lbin" : "sbin", idx, (void *)c);
			return -1;
		}
		kind = (isLarge == 0) ? HZ_K_SBIN : ((c == head) ? HZ_K_TREE : HZ_K_LISTED);
		hz_binPush(c, idx, kind);

		/* _malloc_chunkRemove():298 keys the tree transplant on exactly this
		 * marker, so validate it directly. */
		if (kind == HZ_K_LISTED) {
			if (c->node.parent != &c->node) {
				HZ_FAIL(HZ_V_RB_STRUCTURE,
						"lbin %d: list member %p is not marked not-in-tree (node.parent=%p)",
						idx, (void *)c, (void *)c->node.parent);
				return -1;
			}
		}

		if (c->next == NULL) {
			HZ_FAIL(HZ_V_RB_STRUCTURE, "%s bin %d: NULL next link at %p",
					isLarge ? "lbin" : "sbin", idx, (void *)c);
			return -1;
		}
		if (c->next->prev != c) {
			HZ_FAIL(HZ_V_RB_STRUCTURE, "%s bin %d: broken next/prev at %p",
					isLarge ? "lbin" : "sbin", idx, (void *)c);
			return -1;
		}
		c = c->next;
		if (++guard > 1000000) {
			HZ_FAIL(HZ_V_RB_STRUCTURE, "%s bin %d: list does not close",
					isLarge ? "lbin" : "sbin", idx);
			return -1;
		}
	} while (c != head);

	return 0;
}


/* Recursive tree walk: validates BST order, parent links, the
 * "node.parent == &node means not in the tree" marker, and red/black rules. */
static int hz_collectTree(rbnode_t *n, rbnode_t *parent, int idx, size_t lo, size_t hi, int *blackHeight)
{
	chunk_t *c;
	size_t sz;
	int bl = 0, br = 0;

	if (n == NULL) {
		*blackHeight = 1;
		return 0;
	}

	c = lib_treeof(chunk_t, node, n);
	if (hz_mappedRange(c, CHUNK_MIN_SIZE) == 0) {
		HZ_FAIL(HZ_V_BIN_IN_DEAD_HEAP, "lbin %d: tree node %p unusable -- %s",
				idx, (void *)c, hz_whyBad(c));
		return -1;
	}
	if (n->parent != parent) {
		HZ_FAIL(HZ_V_RB_STRUCTURE, "lbin %d: node %p parent %p != expected %p",
				idx, (void *)c, (void *)n->parent, (void *)parent);
		return -1;
	}
	if (n->parent == n) {
		HZ_FAIL(HZ_V_RB_STRUCTURE, "lbin %d: in-tree node %p is marked not-in-tree", idx, (void *)c);
		return -1;
	}

	sz = hz_chunkSize(c);
	if ((sz <= lo) || (sz > hi)) {
		HZ_FAIL(HZ_V_RB_STRUCTURE, "lbin %d: BST order broken at %p (size %zu, allowed (%zu,%zu])",
				idx, (void *)c, sz, lo, hi);
		return -1;
	}
	if ((n->color == RB_RED) && (parent != NULL) && (parent->color == RB_RED)) {
		HZ_FAIL(HZ_V_RB_STRUCTURE, "lbin %d: red node %p under a red parent", idx, (void *)c);
		return -1;
	}

	/* Every member of this node's duplicate-size list, including the node. */
	if (hz_collectList(c, idx, 1) != 0) {
		return -1;
	}

	if (hz_collectTree(n->left, n, idx, lo, sz - 1, &bl) != 0) {
		return -1;
	}
	if (hz_collectTree(n->right, n, idx, sz, hi, &br) != 0) {
		return -1;
	}
	if (bl != br) {
		HZ_FAIL(HZ_V_RB_STRUCTURE, "lbin %d: black height %d != %d at %p", idx, bl, br, (void *)c);
		return -1;
	}

	*blackHeight = bl + ((n->color == RB_BLACK) ? 1 : 0);
	return 0;
}


static int hz_collectBins(void)
{
	int i, bh;

	hz_nbin = 0;
	hz_binOverflow = 0;

	for (i = 0; i < 32; i++) {
		if (malloc_common.sbins[i] != NULL) {
			if ((malloc_common.sbinmap & (1u << i)) == 0) {
				HZ_FAIL(HZ_V_BINMAP, "sbin %d non-empty but sbinmap bit clear", i);
				return -1;
			}
			if (hz_collectList(malloc_common.sbins[i], i, 0) != 0) {
				return -1;
			}
		}
		else if ((malloc_common.sbinmap & (1u << i)) != 0) {
			HZ_FAIL(HZ_V_BINMAP, "sbinmap bit %d set but bin empty", i);
			return -1;
		}
	}

	for (i = 0; i < 32; i++) {
		if (malloc_common.lbins[i].root != NULL) {
			if ((malloc_common.lbinmap & (1u << i)) == 0) {
				HZ_FAIL(HZ_V_BINMAP, "lbin %d non-empty but lbinmap bit clear", i);
				return -1;
			}
			if (malloc_common.lbins[i].root->parent != NULL) {
				HZ_FAIL(HZ_V_RB_STRUCTURE, "lbin %d root has non-NULL parent", i);
				return -1;
			}
			if (hz_collectTree(malloc_common.lbins[i].root, NULL, i, 0, SIZE_MAX, &bh) != 0) {
				return -1;
			}
		}
		else if ((malloc_common.lbinmap & (1u << i)) != 0) {
			HZ_FAIL(HZ_V_BINMAP, "lbinmap bit %d set but bin empty", i);
			return -1;
		}
	}

	if (hz_binOverflow != 0) {
		fprintf(stderr, "harness: bin snapshot overflow (raise HZ_MAX_BINCHUNKS)\n");
		exit(2);
	}

	return 0;
}


/* ------------------------------------------------------------------ */
/* Heap walk + per-heap invariants.                                     */

/* Walks the chunks of one heap.  out/nout may be NULL.  Returns chunk count or
 * -1 on a corrupt header.  `checkBins` also requires every free chunk to be in
 * the bin snapshot (hz_collectBins() must have run). */
static int hz_walkHeap(heap_t *heap, chunk_t **out, int maxOut, int checkBins)
{
	chunk_t *c = (chunk_t *)heap->space;
	chunk_t *prev = NULL;
	size_t freeSum = 0;
	size_t sz;
	int n = 0;

	for (;;) {
		if (hz_mappedRange(c, CHUNK_MIN_SIZE) == 0) {
			HZ_FAIL(HZ_V_HEAP_WALK_CORRUPT, "heap %p: chunk %p unusable -- %s",
					(void *)heap, (void *)c, hz_whyBad(c));
			return -1;
		}
		if (c->heap != heap) {
			HZ_FAIL(HZ_V_HEAP_WALK_CORRUPT, "heap %p: chunk %p claims heap %p",
					(void *)heap, (void *)c, (void *)c->heap);
			return -1;
		}

		sz = hz_chunkSize(c);
		if ((sz < CHUNK_MIN_SIZE) || ((sz & 7u) != 0) ||
				(((uintptr_t)c + sz) > ((uintptr_t)heap + heap->size))) {
			HZ_FAIL(HZ_V_HEAP_WALK_CORRUPT, "heap %p: chunk %p bad size %zu",
					(void *)heap, (void *)c, sz);
			return -1;
		}

		if (out != NULL && n < maxOut) {
			out[n] = c;
		}
		n++;
		hz_cov.chunksWalked++;
		if (hz_chunkIsFree(c) != 0) {
			hz_cov.freeChunksSeen++;
		}

		/* CHUNK_PUSED must mirror the previous chunk's CHUNK_CUSED. */
		if (prev == NULL) {
			if ((c->size & CHUNK_PUSED) == 0) {
				HZ_FAIL(HZ_V_PUSED_MISMATCH, "heap %p: first chunk %p has PUSED clear",
						(void *)heap, (void *)c);
			}
		}
		else {
			int prevUsed = (prev->size & CHUNK_CUSED) != 0;
			int pused = (c->size & CHUNK_PUSED) != 0;

			if (prevUsed != pused) {
				HZ_FAIL(HZ_V_PUSED_MISMATCH,
						"heap %p: chunk %p PUSED=%d but previous chunk %p CUSED=%d",
						(void *)heap, (void *)c, pused, (void *)prev, prevUsed);
			}
			if ((prevUsed == 0) && (hz_chunkIsFree(c) != 0)) {
				HZ_FAIL(HZ_V_ADJACENT_FREE,
						"heap %p: free chunk %p (size %zu) immediately follows free chunk %p (size %zu)",
						(void *)heap, (void *)c, sz, (void *)prev, hz_chunkSize(prev));
			}
		}

		if (hz_chunkIsFree(c) != 0) {
			size_t foot = *((size_t *)((uintptr_t)c + sz) - 1);

			freeSum += sz;
			if (foot != sz) {
				HZ_FAIL(HZ_V_FOOTER_MISMATCH, "heap %p: free chunk %p size %zu but footer %zu",
						(void *)heap, (void *)c, sz, foot);
			}
			if ((checkBins != 0) && (hz_binSeen(c) == 0)) {
				HZ_FAIL(HZ_V_ORPHAN_FREE_CHUNK,
						"heap %p: free chunk %p (size %zu) is in no bin",
						(void *)heap, (void *)c, sz);
			}
		}

		if (hz_chunkIsLast(c) != 0) {
			if (((uintptr_t)c + sz) != ((uintptr_t)heap + heap->size)) {
				HZ_FAIL(HZ_V_HEAP_WALK_CORRUPT,
						"heap %p: last chunk %p ends at %p, heap ends at %p",
						(void *)heap, (void *)c, (void *)((uintptr_t)c + sz),
						(void *)((uintptr_t)heap + heap->size));
			}
			break;
		}

		prev = c;
		c = (chunk_t *)((uintptr_t)c + sz);
	}

	if (n > hz_cov.maxChunksInHeap) {
		hz_cov.maxChunksInHeap = n;
	}
	/* Not an allocator invariant: a deliberately reachable condition used to
	 * prove the delta-debugging shrinker and the C-repro emitter work, since a
	 * clean stress run leaves them otherwise untested. */
	if ((hz_fragile > 0) && (n > hz_fragile)) {
		HZ_FAIL(HZ_V_CANARY, "heap %p holds %d chunks (> --fragile %d)",
				(void *)heap, n, hz_fragile);
	}
	if (freeSum != heap->freesz) {
		HZ_FAIL(HZ_V_FREESZ_MISMATCH, "heap %p: freesz=%zu but free chunks sum to %zu (%d chunks)",
				(void *)heap, heap->freesz, freeSum, n);
	}

	return n;
}


/* ------------------------------------------------------------------ */
/* The two headline invariants + the structural sweep.                  */

static void hz_checkAll(void)
{
	int i, k;
	heap_t *heap;

	int live = 0;

	if (hz_violation != HZ_OK) {
		return;
	}
	hz_cov.checks++;

	/* The live[] ring must agree with reality.
	 *
	 * Added 2026-09-25 for the defect this harness exists to catch in its own
	 * right: on the Pi, /bin/ntpclient faulted inside malloc_heapSizeValid()
	 * reading a live[] entry that named base 0x5000, a page that was NOT mapped
	 * (run c1hpa01). The allocator's stated invariant is that a non-zero entry
	 * is a heap it still has mapped -- munmap() is the only return-to-OS in the
	 * file and the clear loop is unconditional, exhaustive and runs before it --
	 * so by construction this should be unfalsifiable. It was not. 0 faults in
	 * 878 boots and then one, which is exactly the shape a host harness can hunt
	 * far faster than the bench can.
	 *
	 * Checked against hz_regions[], the harness's OWN shadow of every mmap and
	 * munmap, and against msync() via va2pa() -- both independent of the
	 * bookkeeping under test, so a pass is meaningful rather than circular. */
	for (i = 0; i < 256; i++) {
		uintptr_t lb = malloc_common.live[i];
		size_t ls = malloc_common.liveSize[i];
		int found = 0;

		if (lb == 0u) {
			/* Base and size must be cleared together, or malloc_liveOverlap()
			 * (which now trusts liveSize[] instead of dereferencing) would test
			 * a stale extent against a slot it believes is empty. */
			if (ls != 0u) {
				HZ_FAIL(HZ_V_LIVE_RING,
						"live[%d] base is 0 but liveSize is 0x%zx -- cleared out of step", i, ls);
				return;
			}
			continue;
		}

		for (k = 0; k < hz_nregions; k++) {
			if (hz_regions[k].base != lb) {
				continue;
			}
			found = 1;
			if (hz_regions[k].mapped == 0) {
				HZ_FAIL(HZ_V_LIVE_RING,
						"live[%d] = 0x%lx is a heap already munmap()ed (seq %lu) -- the c1hpa01 signature",
						i, (unsigned long)lb, hz_regions[k].unmapSeq);
				return;
			}
			if (ls != hz_regions[k].size) {
				HZ_FAIL(HZ_V_LIVE_RING,
						"live[%d] = 0x%lx records size 0x%zx but was mmap()ed with 0x%zx",
						i, (unsigned long)lb, ls, hz_regions[k].size);
				return;
			}
			break;
		}

		if (found == 0) {
			HZ_FAIL(HZ_V_LIVE_RING,
					"live[%d] = 0x%lx was NEVER returned by mmap() -- a corrupt slot",
					i, (unsigned long)lb);
			return;
		}

		/* And ask the kernel directly, not just our own table. */
		if (va2pa((void *)lb) == 0u) {
			HZ_FAIL(HZ_V_LIVE_RING,
					"live[%d] = 0x%lx is not mapped (msync) though the region table says it is",
					i, (unsigned long)lb);
			return;
		}
	}

	/* Invariant 2, direct form: nothing reachable from a bin may live in an
	 * unmapped heap.  hz_collectBins() range-checks before every deref, so a
	 * stale chunk is reported instead of faulting. */
	if (hz_collectBins() != 0) {
		return;
	}

	if (hz_nbin > hz_cov.maxBinChunks) {
		hz_cov.maxBinChunks = hz_nbin;
	}

	for (i = 0; i < hz_nregions; i++) {
		if (hz_regions[i].mapped == 0) {
			continue;
		}
		live++;
		heap = (heap_t *)hz_regions[i].base;

		/* Invariant 1: a heap whose freesz says "entirely free" must hold
		 * exactly one chunk. */
		k = hz_walkHeap(heap, NULL, 0, 1);
		if (k < 0) {
			return;
		}
		if ((heap->freesz == (heap->size - HZ_HEAPHDR)) && (k != 1)) {
			HZ_FAIL(HZ_V_FREEHEAP_MULTICHUNK,
					"heap %p: freesz=%zu == size-%zu but walk found %d chunks",
					(void *)heap, heap->freesz, HZ_HEAPHDR, k);
			return;
		}
		if (hz_violation != HZ_OK) {
			return;
		}
	}
	if (live > hz_cov.maxLiveHeaps) {
		hz_cov.maxLiveHeaps = live;
	}

	/* Converse of the orphan check: every bin entry must be a free chunk of a
	 * live heap, in the bin its size selects. */
	for (i = 0; i < hz_nbin; i++) {
		chunk_t *c = hz_bin[i];
		size_t sz = hz_chunkSize(c);

		if (hz_chunkIsFree(c) == 0) {
			HZ_FAIL(HZ_V_GHOST_BIN_CHUNK, "bin chunk %p is marked CUSED", (void *)c);
			return;
		}
		if (hz_regionFindBase((uintptr_t)c->heap) < 0) {
			HZ_FAIL(HZ_V_GHOST_BIN_CHUNK, "bin chunk %p references unknown heap %p",
					(void *)c, (void *)c->heap);
			return;
		}
		if (sz <= CHUNK_SMALLBIN_MAX_SIZE) {
			if ((unsigned)hz_binIdx[i] != malloc_getsidx(sz)) {
				HZ_FAIL(HZ_V_WRONG_BIN, "chunk %p size %zu in sbin %d, expected %u",
						(void *)c, sz, hz_binIdx[i], malloc_getsidx(sz));
				return;
			}
		}
		else if ((unsigned)hz_binIdx[i] != malloc_getlidx(sz)) {
			HZ_FAIL(HZ_V_WRONG_BIN, "chunk %p size %zu in lbin %d, expected %u",
					(void *)c, sz, hz_binIdx[i], malloc_getlidx(sz));
			return;
		}
	}
}


/* Fired from inside hz_munmap, BEFORE the pages go away: this is the exact
 * moment the suspected bug would orphan something. */
static void hz_checkBeforeUnmap(heap_t *heap, size_t len)
{
	uintptr_t lo = (uintptr_t)heap;
	uintptr_t hi = lo + len;
	chunk_t *chunks[4096];
	int n, i, j;

	if (hz_violation != HZ_OK) {
		return;
	}

	n = hz_walkHeap(heap, chunks, 4096, 0);
	if (n < 0) {
		return;
	}

	/* Invariant 1, at the decisive instant.  free():593 assumes "freesz says
	 * fully free" implies "exactly one chunk, at heap->space". */
	if (n != 1) {
		HZ_FAIL(HZ_V_FREEHEAP_MULTICHUNK,
				"heap %p about to be unmapped (freesz=%zu, size-%zu=%zu) still holds %d chunks",
				(void *)heap, heap->freesz, HZ_HEAPHDR, heap->size - HZ_HEAPHDR, n);
		return;
	}

	if (hz_collectBins() != 0) {
		return;
	}

	/* Invariant 2, predictive form: is any chunk of this heap still in a bin? */
	for (i = 0; i < hz_nbin; i++) {
		uintptr_t p = (uintptr_t)hz_bin[i];

		if ((p >= lo) && (p < hi)) {
			HZ_FAIL(HZ_V_PTR_INTO_DYING_HEAP,
					"chunk %p (bin %d) still reachable in heap %p that is being unmapped",
					(void *)hz_bin[i], hz_binIdx[i], (void *)heap);
			return;
		}
		if (((uintptr_t)hz_bin[i]->heap >= lo) && ((uintptr_t)hz_bin[i]->heap < hi)) {
			HZ_FAIL(HZ_V_BIN_IN_DEAD_HEAP,
					"chunk %p references heap %p that is being unmapped",
					(void *)hz_bin[i], (void *)hz_bin[i]->heap);
			return;
		}
	}

	/* Advisor's point: the dangling pointer can live in the bin STRUCTURE
	 * itself (a bin head, or a tree/list link) even when every reachable chunk
	 * is fine -- e.g. if _malloc_chunkRemove(:595) failed to unlink heap->space
	 * completely.  Sweep every raw link for an address inside the dying heap. */
	for (i = 0; i < 32; i++) {
		uintptr_t h = (uintptr_t)malloc_common.sbins[i];

		if ((h >= lo) && (h < hi)) {
			HZ_FAIL(HZ_V_PTR_INTO_DYING_HEAP, "sbins[%d] head %p points into the dying heap %p",
					i, (void *)h, (void *)heap);
			return;
		}
		h = (uintptr_t)malloc_common.lbins[i].root;
		if ((h >= lo) && (h < hi)) {
			HZ_FAIL(HZ_V_PTR_INTO_DYING_HEAP, "lbins[%d].root %p points into the dying heap %p",
					i, (void *)h, (void *)heap);
			return;
		}
	}

	for (i = 0; i < hz_nbin; i++) {
		chunk_t *c = hz_bin[i];
		uintptr_t links[5];
		int nlinks = 2;

		links[0] = (uintptr_t)c->next;
		links[1] = (uintptr_t)c->prev;
		/* Only a real tree node has meaningful left/right/parent; a listed
		 * duplicate carries stale garbage there (see enum HZ_K_*). */
		if (hz_binKind[i] == HZ_K_TREE) {
			links[2] = (uintptr_t)c->node.parent;
			links[3] = (uintptr_t)c->node.left;
			links[4] = (uintptr_t)c->node.right;
			nlinks = 5;
		}

		for (j = 0; j < nlinks; j++) {
			if ((links[j] >= lo) && (links[j] < hi)) {
				HZ_FAIL(HZ_V_PTR_INTO_DYING_HEAP,
						"chunk %p link[%d]=%p points into the dying heap %p",
						(void *)c, j, (void *)links[j], (void *)heap);
				return;
			}
		}
	}
}


/* ------------------------------------------------------------------ */

static int hz_checkUnmap = 1;

static void *hz_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
	void *p = hz_sys_mmap(addr, len, prot, flags, fd, off);

	if (p != MAP_FAILED) {
		hz_regionAdd((uintptr_t)p, len);
		hz_cov.mmaps++;
	}

	return p;
}


static int hz_munmap(void *addr, size_t len)
{
	int i;

	if (hz_checkUnmap != 0) {
		hz_checkBeforeUnmap((heap_t *)addr, len);
	}

	i = hz_regionFindBase((uintptr_t)addr);
	if (i >= 0) {
		hz_regions[i].mapped = 0;
		hz_regions[i].unmapSeq = ++hz_seq;
	}
	if (hz_checkUnmap != 0) {
		hz_cov.munmaps++;
	}

	return hz_sys_munmap(addr, len);
}


/* ------------------------------------------------------------------ */
/* Workload driver.                                                     */

#define HZ_SLOTS   1024
#define HZ_MAX_OPS 400000

enum { OP_NOP = 0, OP_ALLOC, OP_CALLOC, OP_FREE, OP_REALLOC, OP_DRAIN };

typedef struct {
	uint8_t op;
	uint16_t slot;
	uint32_t a;
	uint32_t b;
} hz_op_t;

static hz_op_t hz_ops[HZ_MAX_OPS];
static int hz_nops;
/* Size each op actually used, filled in during execution.  Relative reallocs
 * (b == 1: `a` is a permille of the current size) need this so the C repro
 * emitter can print literal byte counts. */
static uint32_t hz_resolved[HZ_MAX_OPS];

static void *hz_slot[HZ_SLOTS];
static size_t hz_slotSz[HZ_SLOTS];
static uint8_t hz_slotTag[HZ_SLOTS];

static uint64_t hz_rngState;

static uint64_t hz_rnd(void)
{
	uint64_t x = hz_rngState;

	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	hz_rngState = x;
	return x;
}


static uint32_t hz_rndBelow(uint32_t n)
{
	return (n == 0) ? 0 : (uint32_t)(hz_rnd() % n);
}


static void hz_fill(int s)
{
	uint8_t *p = hz_slot[s];
	size_t i, n = hz_slotSz[s];
	uint8_t tag = hz_slotTag[s];

	for (i = 0; i < n; i++) {
		p[i] = (uint8_t)(tag ^ (uint8_t)(i * 31u));
	}
}


static int hz_verify(int s)
{
	const uint8_t *p = hz_slot[s];
	size_t i, n = hz_slotSz[s];
	uint8_t tag = hz_slotTag[s];

	for (i = 0; i < n; i++) {
		if (p[i] != (uint8_t)(tag ^ (uint8_t)(i * 31u))) {
			HZ_FAIL(HZ_V_DATA_CORRUPT, "slot %d (%p, %zu bytes) byte %zu: %02x != %02x",
					s, (void *)p, n, i, p[i], (uint8_t)(tag ^ (uint8_t)(i * 31u)));
			return -1;
		}
	}

	return 0;
}


static void hz_reset(void)
{
	int i;

	hz_checkUnmap = 0;
	for (i = 0; i < hz_nregions; i++) {
		if (hz_regions[i].mapped != 0) {
			hz_sys_munmap((void *)hz_regions[i].base, hz_regions[i].size);
		}
	}
	hz_nregions = 0;
	hz_seq = 0;
	hz_checkUnmap = 1;

	memset(hz_slot, 0, sizeof(hz_slot));
	memset(hz_slotSz, 0, sizeof(hz_slotSz));
	memset(&malloc_common, 0, sizeof(malloc_common));
	phx_malloc_init();
}


/* Size classes chosen to straddle CHUNK_SMALLBIN_MAX_SIZE (= 240 with a 16-byte
 * CHUNK_OVERHEAD) and to make single-heap, single-chunk mappings common so the
 * free()/munmap path is exercised constantly. */
static uint32_t hz_pickSize(void)
{
	uint32_t r = hz_rndBelow(100);

	if (r < 30) {
		return 1 + hz_rndBelow(64);
	}
	if (r < 55) {
		return 1 + hz_rndBelow(239);
	}
	if (r < 75) {
		return 200 + hz_rndBelow(600);
	}
	if (r < 90) {
		return 800 + hz_rndBelow(4000);
	}
	if (r < 98) {
		return 4000 + hz_rndBelow(30000);
	}
	return 30000 + hz_rndBelow(200000);
}


static void hz_emit(int op, int slot, uint32_t a, uint32_t b)
{
	hz_op_t *o;

	if (hz_nops >= HZ_MAX_OPS) {
		return;
	}
	o = &hz_ops[hz_nops++];
	o->op = (uint8_t)op;
	o->slot = (uint16_t)slot;
	o->a = a;
	o->b = b;
}


/* Op streams are FLAT (no compound ops) so the delta-debugging shrinker and the
 * C-repro emitter stay trivial.  The composite patterns below are expanded here.
 *
 * The patterns exist because plain uniform-random alloc/free rarely produces
 * adjacent free chunks: what stresses _malloc_chunkJoin() and the fully-free/
 * munmap branch in free():593 is a *burst* of same-size blocks (which pack
 * consecutively inside one heap) followed by a *strided sweep* that frees them in
 * an order forcing backward joins, forward joins, and eventually a full drain. */
static void hz_gen(uint64_t seed, int nops)
{
	hz_rngState = seed ? seed : 0x9e3779b97f4a7c15ull;
	hz_nops = 0;

	while (hz_nops < nops && hz_nops < HZ_MAX_OPS) {
		uint32_t r = hz_rndBelow(100);

		if (r < 26) {
			/* Burst: same-size blocks land adjacent inside one heap. */
			uint32_t sz = hz_pickSize();
			int base = (int)hz_rndBelow(HZ_SLOTS);
			int n = 2 + (int)hz_rndBelow(120);
			int i;

			for (i = 0; i < n; i++) {
				hz_emit(OP_ALLOC, (base + i) % HZ_SLOTS, sz, 0);
			}
		}
		else if (r < 46) {
			/* Strided sweep: frees neighbours in an interleaved order, which is
			 * what actually exercises both join loops. */
			uint32_t stride = 1 + hz_rndBelow(5);
			uint32_t off = hz_rndBelow(stride);
			int i;

			for (i = 0; i < HZ_SLOTS; i++) {
				if (((uint32_t)i % stride) == off) {
					hz_emit(OP_FREE, i, 0, 0);
				}
			}
		}
		else if (r < 58) {
			/* Mixed-size burst: fragments a heap. */
			int base = (int)hz_rndBelow(HZ_SLOTS);
			int n = 2 + (int)hz_rndBelow(40);
			int i;

			for (i = 0; i < n; i++) {
				hz_emit(OP_ALLOC, (base + i) % HZ_SLOTS, hz_pickSize(), 0);
			}
		}
		else if (r < 66) {
			hz_emit(OP_ALLOC, (int)hz_rndBelow(HZ_SLOTS), hz_pickSize(), 0);
		}
		else if (r < 72) {
			hz_emit(OP_CALLOC, (int)hz_rndBelow(HZ_SLOTS), 1 + hz_rndBelow(64), 1 + hz_rndBelow(64));
		}
		else if (r < 82) {
			hz_emit(OP_FREE, (int)hz_rndBelow(HZ_SLOTS), 0, 0);
		}
		else if (r < 86) {
			/* realloc run, sizes RELATIVE to the current block so the in-place
			 * branches are actually taken rather than degenerating into
			 * malloc+memcpy+free. */
			int base = (int)hz_rndBelow(HZ_SLOTS);
			int n = 1 + (int)hz_rndBelow(24);
			int i;

			for (i = 0; i < n; i++) {
				hz_emit(OP_REALLOC, (base + i) % HZ_SLOTS, 50 + hz_rndBelow(1900), 1);
			}
		}
		else if (r < 91) {
			/* Deterministic realloc SHRINK-SPLIT (malloc_dl.c:633): allocate,
			 * then ask for a fraction that leaves >= CHUNK_MIN_SIZE over. */
			uint32_t sz = 400 + hz_rndBelow(8000);
			int base = (int)hz_rndBelow(HZ_SLOTS);
			int n = 1 + (int)hz_rndBelow(16);
			int i;

			for (i = 0; i < n; i++) {
				hz_emit(OP_ALLOC, (base + i) % HZ_SLOTS, sz, 0);
			}
			for (i = 0; i < n; i++) {
				hz_emit(OP_REALLOC, (base + i) % HZ_SLOTS, 100 + hz_rndBelow(600), 1);
			}
		}
		else if (r < 96) {
			/* Realloc GROW-IN-PLACE (malloc_dl.c:647) needs the *next* chunk to
			 * be free and large enough.  Build that state on purpose: pack a
			 * burst of same-size blocks (adjacent inside one heap), free every
			 * other one, then grow the survivors into the holes. */
			uint32_t sz = 64 + hz_rndBelow(1500);
			int base = (int)hz_rndBelow(HZ_SLOTS);
			int n = 8 + (int)hz_rndBelow(80);
			int i;

			for (i = 0; i < n; i++) {
				hz_emit(OP_FREE, (base + i) % HZ_SLOTS, 0, 0);
			}
			for (i = 0; i < n; i++) {
				hz_emit(OP_ALLOC, (base + i) % HZ_SLOTS, sz, 0);
			}
			for (i = 1; i < n; i += 2) {
				hz_emit(OP_FREE, (base + i) % HZ_SLOTS, 0, 0);
			}
			for (i = 0; i < n; i += 2) {
				hz_emit(OP_REALLOC, (base + i) % HZ_SLOTS, 1050 + hz_rndBelow(900), 1);
			}
		}
		else {
			/* Full drain: the only way every heap reaches free():593. */
			hz_emit(OP_DRAIN, 0, 0, 0);
		}
	}
}


static int hz_checkEvery = 1;

static void hz_applyOp(const hz_op_t *o, long idx)
{
	int s = o->slot % HZ_SLOTS;
	void *p;

	switch (o->op) {
		case OP_ALLOC:
			if (hz_slot[s] != NULL) {
				if (hz_verify(s) != 0) {
					return;
				}
				phx_free(hz_slot[s]);
				hz_slot[s] = NULL;
			}
			p = phx_malloc(o->a);
			if (p != NULL) {
				hz_slot[s] = p;
				hz_slotSz[s] = o->a;
				hz_slotTag[s] = (uint8_t)(idx ^ s ^ 0xa5);
				hz_fill(s);
			}
			break;

		case OP_CALLOC:
			if (hz_slot[s] != NULL) {
				if (hz_verify(s) != 0) {
					return;
				}
				phx_free(hz_slot[s]);
				hz_slot[s] = NULL;
			}
			p = phx_calloc(o->a, o->b);
			if (p != NULL) {
				size_t n = (size_t)o->a * o->b, i;
				const uint8_t *q = p;

				for (i = 0; i < n; i++) {
					if (q[i] != 0) {
						HZ_FAIL(HZ_V_DATA_CORRUPT, "calloc(%u,%u) byte %zu not zeroed",
								o->a, o->b, i);
						return;
					}
				}
				hz_slot[s] = p;
				hz_slotSz[s] = n;
				hz_slotTag[s] = (uint8_t)(idx ^ s ^ 0x5a);
				hz_fill(s);
			}
			break;

		case OP_FREE:
			if (hz_slot[s] != NULL) {
				chunk_t *fc = (chunk_t *)((uintptr_t)hz_slot[s] - CHUNK_OVERHEAD);
				chunk_t *fn = malloc_chunkNext(fc);

				if (hz_verify(s) != 0) {
					return;
				}
				if ((fc->size & CHUNK_PUSED) == 0) {
					hz_cov.joinBackward++;
				}
				if ((fn != NULL) && ((fn->size & CHUNK_CUSED) == 0)) {
					hz_cov.joinForward++;
				}
				phx_free(hz_slot[s]);
				hz_slot[s] = NULL;
				hz_slotSz[s] = 0;
			}
			break;

		case OP_REALLOC:
			if (hz_slot[s] == NULL) {
				p = phx_malloc(o->a);
				if (p != NULL) {
					hz_slot[s] = p;
					hz_slotSz[s] = o->a;
					hz_slotTag[s] = (uint8_t)(idx ^ s ^ 0x3c);
					hz_fill(s);
				}
			}
			else {
				size_t old = hz_slotSz[s];
				uint32_t want = o->a;

				if (o->b == 1) {
					want = (uint32_t)((old * (size_t)o->a) / 1000u);
					if (want == 0) {
						want = 1;
					}
				}
				if ((idx >= 0) && (idx < HZ_MAX_OPS)) {
					hz_resolved[idx] = want;
				}

				if (hz_verify(s) != 0) {
					return;
				}
				{
					chunk_t *rc = (chunk_t *)((uintptr_t)hz_slot[s] - CHUNK_OVERHEAD);
					size_t before = malloc_chunkSize(rc);
					size_t chunkWant = CEIL(max(want + CHUNK_OVERHEAD, CHUNK_MIN_SIZE), 8);

					if ((chunkWant < before) && malloc_chunkCanSplit(rc, chunkWant)) {
						hz_cov.reallocShrinkSplit++;
					}
					else if (chunkWant > before) {
						chunk_t *rn = malloc_chunkNext(rc);

						if ((rn != NULL) && ((rn->size & CHUNK_CUSED) == 0) &&
								(malloc_chunkSize(rn) >= (chunkWant - before))) {
							hz_cov.reallocGrowInPlace++;
						}
					}
				}
				p = phx_realloc(hz_slot[s], want);
				if (p != NULL) {
					size_t keep = (old < want) ? old : want;
					const uint8_t *q = p;
					size_t i;

					for (i = 0; i < keep; i++) {
						if (q[i] != (uint8_t)(hz_slotTag[s] ^ (uint8_t)(i * 31u))) {
							HZ_FAIL(HZ_V_DATA_CORRUPT,
									"realloc(%zu -> %u) lost byte %zu", old, want, i);
							return;
						}
					}
					hz_slot[s] = p;
					hz_slotSz[s] = want;
					hz_fill(s);
				}
			}
			break;

		case OP_DRAIN: {
			int i;

			for (i = 0; i < HZ_SLOTS; i++) {
				if (hz_slot[i] != NULL) {
					if (hz_verify(i) != 0) {
						return;
					}
					phx_free(hz_slot[i]);
					hz_slot[i] = NULL;
					hz_slotSz[i] = 0;
				}
			}
			break;
		}

		default:
			break;
	}
}


static sigjmp_buf hz_jmp;
static volatile int hz_jmpArmed;

static void hz_segv(int sig, siginfo_t *si, void *uc)
{
	(void)sig;
	(void)uc;

	if (hz_jmpArmed != 0) {
		hz_jmpArmed = 0;
		HZ_FAIL(HZ_V_SEGV, "SIGSEGV at %p (page-aligned: %s)", si->si_addr,
				(((uintptr_t)si->si_addr & (_PAGE_SIZE - 1)) == 0) ? "yes" : "no");
		siglongjmp(hz_jmp, 1);
	}
	_exit(3);
}


/* Runs ops[0..n) on a freshly reset allocator.  Returns the violation code. */
static int hz_run(const hz_op_t *ops, int n)
{
	long i;

	hz_violation = HZ_OK;
	hz_vdetail[0] = '\0';
	hz_vop = -1;
	hz_reset();

	hz_jmpArmed = 1;
	if (sigsetjmp(hz_jmp, 1) == 0) {
		for (i = 0; i < n; i++) {
			hz_applyOp(&ops[i], i);
			if (hz_violation != HZ_OK) {
				hz_vop = i;
				break;
			}
			if ((hz_checkEvery == 1) || ((i % hz_checkEvery) == 0) || (ops[i].op == OP_DRAIN)) {
				hz_checkAll();
			}
			if (hz_violation != HZ_OK) {
				hz_vop = i;
				break;
			}
		}
		if (hz_violation == HZ_OK) {
			/* Final drain: everything must come back and every heap must go. */
			hz_op_t d = { OP_DRAIN, 0, 0, 0 };

			hz_applyOp(&d, n);
			hz_checkAll();
			if (hz_violation != HZ_OK) {
				hz_vop = n;
			}
		}
	}
	hz_jmpArmed = 0;

	return hz_violation;
}


/* ------------------------------------------------------------------ */
/* Delta-debugging shrinker: drop ops while the same violation persists. */

static hz_op_t hz_best[HZ_MAX_OPS];
static hz_op_t hz_try[HZ_MAX_OPS];

static void hz_emitRepro(const hz_op_t *ops, int n)
{
	int i;
	int used[HZ_SLOTS] = { 0 };

	printf("/* ---- minimal reproducing sequence (%d ops) ---- */\n", n);
	printf("void *p[%d] = { 0 };\n", HZ_SLOTS);
	for (i = 0; i < n; i++) {
		int s = ops[i].slot % HZ_SLOTS;

		switch (ops[i].op) {
			case OP_ALLOC:
				if (used[s]) {
					printf("free(p[%d]);\n", s);
				}
				printf("p[%d] = malloc(%u);\n", s, ops[i].a);
				used[s] = 1;
				break;
			case OP_CALLOC:
				if (used[s]) {
					printf("free(p[%d]);\n", s);
				}
				printf("p[%d] = calloc(%u, %u);\n", s, ops[i].a, ops[i].b);
				used[s] = 1;
				break;
			case OP_FREE:
				if (used[s]) {
					printf("free(p[%d]); p[%d] = 0;\n", s, s);
					used[s] = 0;
				}
				break;
			case OP_REALLOC: {
				unsigned sz = (ops[i].b == 1) ? hz_resolved[i] : ops[i].a;

				if (used[s]) {
					printf("p[%d] = realloc(p[%d], %u);\n", s, s, sz);
				}
				else {
					printf("p[%d] = malloc(%u);\n", s, ops[i].a);
					used[s] = 1;
				}
				break;
			}
			case OP_DRAIN: {
				int j;

				for (j = 0; j < HZ_SLOTS; j++) {
					if (used[j]) {
						printf("free(p[%d]); p[%d] = 0;\n", j, j);
						used[j] = 0;
					}
				}
				break;
			}
			default:
				break;
		}
	}
	printf("/* ---- end ---- */\n");
}


static void hz_shrink(int target, int n)
{
	int bestN = n;
	int gran, changed = 1, pass = 0;

	memcpy(hz_best, hz_ops, (size_t)n * sizeof(hz_op_t));
	hz_quiet = 1;
	hz_checkEvery = 1;

	while (changed != 0) {
		changed = 0;
		pass++;
		for (gran = bestN / 2; gran >= 1; gran /= 2) {
			int i = 0;

			while (i < bestN) {
				int cut = (gran < (bestN - i)) ? gran : (bestN - i);
				int m = 0;

				memcpy(hz_try, hz_best, (size_t)i * sizeof(hz_op_t));
				m = i;
				memcpy(hz_try + m, hz_best + i + cut, (size_t)(bestN - i - cut) * sizeof(hz_op_t));
				m += bestN - i - cut;

				if (hz_run(hz_try, m) == target) {
					memcpy(hz_best, hz_try, (size_t)m * sizeof(hz_op_t));
					bestN = m;
					changed = 1;
					/* keep i, the window now holds fresh ops */
				}
				else {
					i += cut;
				}
			}
			if (gran == 1) {
				break;
			}
		}
		if (pass > 8) {
			break;
		}
	}

	hz_quiet = 0;
	fprintf(stderr, "shrunk %d -> %d ops\n", n, bestN);
	if (hz_run(hz_best, bestN) == target) {
		fprintf(stderr, "minimal sequence reproduces: %s\n", hz_vname[target]);
	}
	hz_emitRepro(hz_best, bestN);
}



/* ------------------------------------------------------------------ */
/* Detector self-test.  A clean stress run only means something if the checker
 * can actually see the bug being hunted, so forge each violation by hand and
 * require the corresponding report.  ("Verify the measurement, not the result.") */

static int hz_expect(int want, const char *label)
{
	int ok = (hz_violation == want);

	printf("selftest %-22s -> %s (%s)%s\n", label, hz_vname[hz_violation],
			(hz_vdetail[0] != '\0') ? hz_vdetail : "-", ok ? "" : "   <== DETECTOR FAILED");
	return ok ? 0 : 1;
}


static int hz_selftest(void)
{
	void *p, *a, *b;
	chunk_t *c, *fake, *cb;
	heap_t *h;
	int bad = 0;

	hz_quiet = 1;

	/* (1) INV2b: a chunk left in a global bin while its heap is unmapped --
	 * literally what free():593-597 would do if a heap that "freesz says is
	 * fully free" still held a second binned chunk. */
	hz_reset();
	hz_violation = HZ_OK;
	hz_vdetail[0] = '\0';
	p = phx_malloc(3000); /* gets a heap of its own */
	c = (chunk_t *)((uintptr_t)p - CHUNK_OVERHEAD);
	h = c->heap;
	fake = (chunk_t *)((uintptr_t)h + 2048);
	malloc_chunkInit(fake, h, 64);
	fake->size |= CHUNK_PUSED;
	_malloc_chunkAdd(fake); /* orphan-to-be: in a bin, inside this heap */
	phx_free(p);            /* heap now "fully free" -> munmap */
	bad += hz_expect(HZ_V_PTR_INTO_DYING_HEAP, "orphan at munmap");

	/* (2) INV2: the same orphan seen after the unmap, by the bin sweep. */
	hz_reset();
	hz_violation = HZ_OK;
	hz_vdetail[0] = '\0';
	hz_checkUnmap = 0; /* silence the pre-unmap detector so the post sweep runs */
	p = phx_malloc(3000);
	c = (chunk_t *)((uintptr_t)p - CHUNK_OVERHEAD);
	h = c->heap;
	fake = (chunk_t *)((uintptr_t)h + 2048);
	malloc_chunkInit(fake, h, 64);
	fake->size |= CHUNK_PUSED;
	_malloc_chunkAdd(fake);
	phx_free(p);
	hz_checkUnmap = 1;
	hz_checkAll();
	bad += hz_expect(HZ_V_BIN_IN_DEAD_HEAP, "orphan after munmap");

	/* (2f) The BO-attribution call site actually EXECUTES, and is handed the
	 * page-aligned physical address.
	 *
	 * On hardware this only runs when C1 fires (~1 run in 16), so a mis-wired call
	 * site would cost hours and a rare event to discover. Drive the reporter
	 * directly instead and check the fake table was consulted. */
	{
		void *bp = phx_malloc(64);
		chunk_t *bc = (chunk_t *)((uintptr_t)bp - CHUNK_OVERHEAD);
		heap_t *bh = bc->heap;
		int calls0 = hz_boLookupCalls;
		int ok;

		hz_quiet = 1;                 /* the reporter is chatty; we want the call, not the noise */
		malloc_reportHeapSize(bh);
		hz_quiet = 0;

		ok = (hz_boLookupCalls == calls0 + 1)
			&& (hz_boLookupArg == ((unsigned long)va2pa((void *)((uintptr_t)bh & ~(uintptr_t)(_PAGE_SIZE - 1)))));
		printf("selftest %-22s -> %s (calls +%d, arg 0x%lx)%s\n", "BO attribution wired",
			ok ? "called with the page PA" : "NOT CALLED",
			hz_boLookupCalls - calls0, hz_boLookupArg,
			ok ? "" : "   <== DETECTOR FAILED");
		bad += ok ? 0 : 1;
		phx_free(bp);
	}

	/* (2c-e) The live[] ring checker, in its three failure modes. A detector for
	 * a defect seen ONCE on hardware is worthless unless it is shown to fire, so
	 * forge each one. Slot 255 is used throughout: liveIdx starts at 0 and this
	 * workload never wraps, so writing there cannot evict a real entry. */
	{
		int ri, done = 0;

		/* (2c) The c1hpa01 signature itself: an entry naming a heap that has
		 * been munmap()ed. Forged from the region table's own tombstone, so the
		 * address is one the allocator really did map and really did release --
		 * not a value invented by the test. */
		hz_reset();
		hz_violation = HZ_OK;
		hz_vdetail[0] = '\0';
		p = phx_malloc(64);
		phx_free(p);
		for (ri = 0; ri < hz_nregions; ri++) {
			if (hz_regions[ri].mapped == 0) {
				malloc_common.live[255] = hz_regions[ri].base;
				malloc_common.liveSize[255] = hz_regions[ri].size;
				done = 1;
				break;
			}
		}
		if (done == 0) {
			printf("selftest %-22s -> NO RELEASED HEAP -- case NOT exercised   <== DETECTOR FAILED\n",
					"live[] stale entry");
			bad++;
		}
		else {
			hz_checkAll();
			bad += hz_expect(HZ_V_LIVE_RING, "live[] stale entry");
		}
		malloc_common.live[255] = 0u;
		malloc_common.liveSize[255] = 0u;

		/* (2d) A slot holding an address that was never an mmap() return at all
		 * -- the "corrupt slot" reading. 0x5000 is the literal value that killed
		 * ntpclient. */
		hz_reset();
		hz_violation = HZ_OK;
		hz_vdetail[0] = '\0';
		malloc_common.live[255] = 0x5000u;
		malloc_common.liveSize[255] = 0x1000u;
		hz_checkAll();
		bad += hz_expect(HZ_V_LIVE_RING, "live[] never-mmapped base");
		malloc_common.live[255] = 0u;
		malloc_common.liveSize[255] = 0u;

		/* (2e) Base and size cleared out of step. malloc_liveOverlap() now trusts
		 * liveSize[] instead of dereferencing, so a stale size on an empty slot
		 * would have it test a phantom extent. */
		hz_reset();
		hz_violation = HZ_OK;
		hz_vdetail[0] = '\0';
		malloc_common.live[255] = 0u;
		malloc_common.liveSize[255] = 0x1000u;
		hz_checkAll();
		bad += hz_expect(HZ_V_LIVE_RING, "live[] size without base");
		malloc_common.liveSize[255] = 0u;
	}

	/* (3) INV1: a fully-free heap holding two chunks. */
	hz_reset();
	hz_violation = HZ_OK;
	hz_vdetail[0] = '\0';
	a = phx_malloc(64);
	b = phx_malloc(64);
	c = (chunk_t *)((uintptr_t)a - CHUNK_OVERHEAD);
	cb = (chunk_t *)((uintptr_t)b - CHUNK_OVERHEAD);
	h = c->heap;
	phx_free(a);
	/* Mark b free and bin it WITHOUT coalescing, then claim the heap is empty. */
	cb->size &= ~CHUNK_CUSED;
	malloc_chunkSetFooter(cb);
	h->freesz += malloc_chunkSize(cb);
	_malloc_chunkAdd(cb);
	hz_checkAll();
	bad += hz_expect(HZ_V_ADJACENT_FREE, "unmerged neighbours");

	/* (4) freesz drift. */
	hz_reset();
	hz_violation = HZ_OK;
	hz_vdetail[0] = '\0';
	p = phx_malloc(64);
	c = (chunk_t *)((uintptr_t)p - CHUNK_OVERHEAD);
	c->heap->freesz -= 8;
	hz_checkAll();
	bad += hz_expect(HZ_V_FREESZ_MISMATCH, "freesz drift");
	c->heap->freesz += 8;

	/* (5) a free chunk that is in no bin. */
	hz_reset();
	hz_violation = HZ_OK;
	hz_vdetail[0] = '\0';
	a = phx_malloc(64);
	b = phx_malloc(64);
	cb = (chunk_t *)((uintptr_t)b - CHUNK_OVERHEAD);
	phx_free(a);
	cb->size &= ~CHUNK_CUSED;
	malloc_chunkSetFooter(cb);
	cb->heap->freesz += malloc_chunkSize(cb);
	hz_checkAll();
	bad += (hz_violation == HZ_V_ORPHAN_FREE_CHUNK || hz_violation == HZ_V_ADJACENT_FREE) ? 0 : 1;
	printf("selftest %-22s -> %s (%s)\n", "free chunk in no bin", hz_vname[hz_violation], hz_vdetail);

	hz_reset();
	hz_violation = HZ_OK;
	hz_vdetail[0] = '\0';
	hz_quiet = 0;
	printf("selftest: %d detector failure(s)\n", bad);
	return bad;
}

/* ------------------------------------------------------------------ */
/* Multithreaded stress -- the axis the loop above cannot reach.
 *
 * The randomized stress is single-threaded, and on 2026-09-17 it ran 8 seeds x
 * 300k ops with zero violations while the on-target guards were firing in two
 * heavily multithreaded apps. So drive the SAME allocator from several threads
 * with a real lock underneath (stubs/sys/threads.h is a pthread mutex since the
 * same date -- with the old no-op stub an MT run would have proved nothing).
 *
 * The invariant checker walks global allocator state and is not itself
 * thread-safe, so it runs once after the join rather than during. What runs
 * DURING is a tag check: every block's first and last 64 bytes carry its own
 * byte, verified before every realloc and free, so another thread writing into
 * a live block is caught in the act instead of being inferred from a later
 * crash. Under --asan the same run also catches every out-of-bounds touch.
 */
typedef struct {
	unsigned int id;
	unsigned long ops;
	uint64_t rng;
	unsigned long allocs;
	unsigned long frees;
	unsigned long reallocs;
	unsigned long mism;
	unsigned long oom;
} hz_mt_t;

#define HZ_MT_LIVE 48u

static uint64_t hz_mtRnd(uint64_t *s)
{
	uint64_t x = *s;

	x ^= x >> 12;
	x ^= x << 25;
	x ^= x >> 27;
	*s = x;
	return x * 0x2545f4914f6cdd1dULL;
}


static size_t hz_mtSize(uint64_t *s)
{
	uint64_t r = hz_mtRnd(s);

	/* One draw in 64 is big enough that freeing it can empty a heap and hand it
	 * back while other threads are still allocating -- the window worth hunting. */
	if ((r & 63u) == 0u) {
		return (size_t)(64u * 1024u + (r >> 6) % (192u * 1024u));
	}
	if ((r & 15u) == 0u) {
		return (size_t)(4096u + (r >> 4) % (28u * 1024u));
	}
	return (size_t)(16u + (r >> 4) % 1008u);
}


static void hz_mtFill(void *p, size_t sz, unsigned char tag)
{
	size_t n = (sz < 64u) ? sz : 64u;

	memset(p, (int)tag, n);
	if (sz > 128u) {
		memset((char *)p + sz - 64u, (int)tag, 64u);
	}
}


static int hz_mtCheck(const void *p, size_t sz, unsigned char tag)
{
	const unsigned char *b = (const unsigned char *)p;
	size_t n = (sz < 64u) ? sz : 64u;
	size_t i;

	for (i = 0; i < n; i++) {
		if (b[i] != tag) {
			return 0;
		}
	}
	if (sz > 128u) {
		b = (const unsigned char *)p + sz - 64u;
		for (i = 0; i < 64u; i++) {
			if (b[i] != tag) {
				return 0;
			}
		}
	}
	return 1;
}


static void *hz_mtWorker(void *arg)
{
	hz_mt_t *w = (hz_mt_t *)arg;
	void *ptr[HZ_MT_LIVE];
	size_t sz[HZ_MT_LIVE];
	unsigned char tag[HZ_MT_LIVE];
	unsigned int n = 0;
	unsigned long i;

	memset(ptr, 0, sizeof(ptr));
	memset(sz, 0, sizeof(sz));
	memset(tag, 0, sizeof(tag));

	for (i = 0; i < w->ops; i++) {
		uint64_t r = hz_mtRnd(&w->rng);
		unsigned int op = (unsigned int)(r % 100u);
		unsigned int k;

		if ((n == HZ_MT_LIVE) || ((op < 30u) && (n > 0u))) {
			k = (unsigned int)((r >> 8) % n);
			if (hz_mtCheck(ptr[k], sz[k], tag[k]) == 0) {
				w->mism++;
				printf("mt[%u]: TAG MISMATCH before free, %p size %zu tag 0x%02x\n",
						w->id, ptr[k], sz[k], tag[k]);
			}
			phx_free(ptr[k]);
			w->frees++;
			ptr[k] = ptr[n - 1u];
			sz[k] = sz[n - 1u];
			tag[k] = tag[n - 1u];
			n--;
		}
		else if ((op < 45u) && (n > 0u)) {
			size_t ns = hz_mtSize(&w->rng);
			void *np;

			k = (unsigned int)((r >> 8) % n);
			if (hz_mtCheck(ptr[k], sz[k], tag[k]) == 0) {
				w->mism++;
				printf("mt[%u]: TAG MISMATCH before realloc, %p size %zu\n",
						w->id, ptr[k], sz[k]);
			}
			np = phx_realloc(ptr[k], ns);
			if (np == NULL) {
				w->oom++;
				continue;
			}
			ptr[k] = np;
			sz[k] = ns;
			tag[k] = (unsigned char)((r >> 16) | 1u);
			hz_mtFill(ptr[k], sz[k], tag[k]);
			w->reallocs++;
		}
		else {
			size_t ns = hz_mtSize(&w->rng);
			void *p = ((r & 7u) == 0u) ? phx_calloc(1u, ns) : phx_malloc(ns);

			if (p == NULL) {
				w->oom++;
				continue;
			}
			ptr[n] = p;
			sz[n] = ns;
			tag[n] = (unsigned char)((r >> 16) | 1u);
			hz_mtFill(ptr[n], sz[n], tag[n]);
			n++;
			w->allocs++;
		}
	}

	while (n > 0u) {
		n--;
		if (hz_mtCheck(ptr[n], sz[n], tag[n]) == 0) {
			w->mism++;
			printf("mt[%u]: TAG MISMATCH at drain, %p size %zu\n", w->id, ptr[n], sz[n]);
		}
		phx_free(ptr[n]);
		w->frees++;
	}
	return NULL;
}


static int hz_mtStress(unsigned int threads, unsigned long ops)
{
	pthread_t tid[32];
	hz_mt_t w[32];
	unsigned int i;
	unsigned long allocs = 0, frees = 0, reallocs = 0, mism = 0, oom = 0;
	int bad = 0;

	if (threads > 32u) {
		threads = 32u;
	}
	hz_reset();
	memset(w, 0, sizeof(w));

	for (i = 0; i < threads; i++) {
		w[i].id = i;
		w[i].ops = ops;
		w[i].rng = 0x9e3779b97f4a7c15ULL ^ ((uint64_t)(i + 1u) * 0x100000001b3ULL);
		if (pthread_create(&tid[i], NULL, hz_mtWorker, &w[i]) != 0) {
			printf("mt: pthread_create(%u) failed\n", i);
			return 1;
		}
	}
	for (i = 0; i < threads; i++) {
		pthread_join(tid[i], NULL);
		allocs += w[i].allocs;
		frees += w[i].frees;
		reallocs += w[i].reallocs;
		mism += w[i].mism;
		oom += w[i].oom;
	}

	/* Single-threaded again: now the structural checker can run. */
	hz_violation = HZ_OK;
	hz_vdetail[0] = '\0';
	hz_checkAll();
	printf("mt: threads=%u ops/thread=%lu  allocs=%lu frees=%lu reallocs=%lu oom=%lu\n",
			threads, ops, allocs, frees, reallocs, oom);
	printf("mt: tag mismatches=%lu   post-join invariants: %s (%s)\n", mism,
			hz_vname[hz_violation], (hz_vdetail[0] != '\0') ? hz_vdetail : "-");
	if (mism != 0u) {
		bad = 1;
	}
	if (hz_violation != HZ_OK) {
		bad = 1;
	}
	return bad;
}


/* ------------------------------------------------------------------ */
/* malloc_chunkValidWhy() code coverage.
 *
 * The corrupt-header report prints a code 1-8 saying WHICH test rejected the
 * header, because those tests answer different questions: 2 means a pointer
 * into a heap we already released (a use-after-free of a whole heap), 7 means a
 * smashed size in a live one, 4 means a header fabricated out of unrelated
 * memory. Three contained field fires in SuperTuxKart (2026-09-15/16/17) could
 * not be told apart without it.
 *
 * Those fires are rare, so the codes have to be right the FIRST time one is
 * read. Each case below is built to fail exactly one test, with every other
 * test passing, and asserts the code -- the same "can the detector actually
 * fire?" discipline as hz_selftest() above. Everything is restored afterwards
 * so the allocator stays usable for the stress run. */
static int hz_whyExpect(int got, int want, const char *label)
{
	int ok = (got == want);

	printf("selftest why %-24s -> %d (want %d)%s\n", label, got, want,
			ok ? "" : "   <== WRONG CODE");
	return ok ? 0 : 1;
}


static int hz_whySelftest(void)
{
	void *p;
	chunk_t *c;
	heap_t *h;
	size_t savedChunkSize, savedHeapSize;
	int bad = 0;

	/* Must run AFTER the allocator is initialised -- this is the first thing in
	 * main() that allocates, and with the old no-op mutex stub calling it first
	 * was silently fine. A real lock turns that into an abort, which is the
	 * correct behaviour: on target _malloc_init() runs in libc init before main. */
	hz_reset();

	p = phx_malloc(64);
	if (p == NULL) {
		printf("selftest why: malloc failed\n");
		return 1;
	}
	c = (chunk_t *)((uintptr_t)p - CHUNK_OVERHEAD);
	h = c->heap;

	/* 0: the unmodified block must be accepted, or every case below is vacuous. */
	bad += hz_whyExpect(malloc_chunkValidWhy(c, h), 0, "a real live block");

	/* 1: chunk pointer that cannot be an address. */
	bad += hz_whyExpect(malloc_chunkValidWhy(NULL, h), 1, "chunk NULL");
	bad += hz_whyExpect(malloc_chunkValidWhy((chunk_t *)((uintptr_t)1 << 47), h), 1,
			"chunk non-canonical");
	bad += hz_whyExpect(malloc_chunkValidWhy((chunk_t *)((uintptr_t)c + 1), h), 1,
			"chunk misaligned");

	/* 2: a pointer into a heap we have already released. Note it by hand rather
	 * than racing a real munmap, then clear the entry again. */
	{
		unsigned int slot = malloc_common.relIdx % 8u;
		malloc_common.released[slot].base = (uintptr_t)h;
		malloc_common.released[slot].size = h->size;
		bad += hz_whyExpect(malloc_chunkValidWhy(c, h), 2, "chunk in a released heap");
		malloc_common.released[slot].base = 0;
		malloc_common.released[slot].size = 0;
	}

	/* 3: heap pointer NULL or not page-aligned. */
	bad += hz_whyExpect(malloc_chunkValidWhy(c, NULL), 3, "heap NULL");
	bad += hz_whyExpect(malloc_chunkValidWhy(c, (heap_t *)((uintptr_t)h + 8)), 3,
			"heap unaligned");

	/* 4: page-aligned heap outside the window we have actually mmap'd. Never
	 * dereferenced, because the window test rejects it first. */
	if (malloc_common.heapHi != 0u) {
		bad += hz_whyExpect(malloc_chunkValidWhy(c, (heap_t *)(uintptr_t)0x1000), 4,
				"heap outside [lo,hi)");
	}

	/* 5: the heap's own size is not sane for its base. */
	savedHeapSize = h->size;
	h->size = 1u;
	bad += hz_whyExpect(malloc_chunkValidWhy(c, h), 5, "heap->size insane");
	h->size = savedHeapSize;

	/* 6: chunk outside its heap's range -- just past the end, still canonical
	 * and aligned, and not dereferenced before the range test. */
	bad += hz_whyExpect(malloc_chunkValidWhy(
			(chunk_t *)((uintptr_t)h + h->size + 8u), h), 6, "chunk past heap end");

	/* 7: chunk size below the minimum (flag bits preserved). */
	savedChunkSize = c->size;
	c->size = (savedChunkSize & (CHUNK_CUSED | CHUNK_PUSED)) | 8u;
	bad += hz_whyExpect(malloc_chunkValidWhy(c, h), 7, "chunk size too small");
	c->size = savedChunkSize;

	/* 8: a size that passes the minimum and alignment tests but runs off the end. */
	c->size = (savedChunkSize & (CHUNK_CUSED | CHUNK_PUSED)) | (h->size + 0x1000u);
	bad += hz_whyExpect(malloc_chunkValidWhy(c, h), 8, "chunk runs past heap end");
	c->size = savedChunkSize;

	/* malloc_liveOverlap(): the check that says mmap handed back a region sitting on
	 * a live heap. A randomized stress run can only ever show it NOT firing, which is
	 * exactly the "grader that cannot fail" shape, so prove it reports.
	 *
	 * Probe against a REAL live heap rather than a planted one: malloc_heapSizeValid()
	 * demands a page-aligned base inside [heapLo, heapHi), so a stack buffer is skipped
	 * as insane and every positive case silently passes. (It did, on the first run.)
	 *
	 * Heaps pack back-to-back, so "just after this heap" may legitimately belong to the
	 * next one -- the negative case therefore probes below heapLo, where no live heap
	 * can be. */
	{
		uintptr_t lbase = 0u;
		size_t lsize = 0u;
		unsigned int li;

		for (li = 0; li < 256u; li++) {
			if ((malloc_common.live[li] != 0u)
					&& (malloc_heapSizeValid((const heap_t *)malloc_common.live[li]) != 0)) {
				lbase = malloc_common.live[li];
				lsize = ((const heap_t *)lbase)->size;
				break;
			}
		}

		if (lbase == 0u) {
			printf("selftest why overlap: NO LIVE HEAP -- check not exercised\n");
			bad++;
		}
		else {
			/* malloc_liveOverlap now hands back the OVERLAPPED heap's recorded
			 * size through an out-param, so it never dereferences a live[]
			 * entry (2026-09-25). Check that size too: it is the field the
			 * not-mapped report leans on, and an out-param nobody asserts is an
			 * out-param that can silently rot. */
			size_t ovs = 0u;

			bad += hz_whyExpect(malloc_liveOverlap(lbase + 16u, 256u, &ovs) == lbase, 1,
					"overlap: inside a live heap");
			bad += hz_whyExpect(ovs == lsize, 1, "overlap: reports the live heap's size");
			bad += hz_whyExpect(malloc_liveOverlap(lbase, lsize, &ovs) == lbase, 1,
					"overlap: same base (total)");
			bad += hz_whyExpect(malloc_liveOverlap(lbase + lsize - 16u, 32u, &ovs) == lbase, 1,
					"overlap: straddles the end");
			ovs = 0xdeadu;
			bad += hz_whyExpect(
					malloc_liveOverlap(malloc_common.heapLo - 0x2000u, 0x1000u, &ovs) == 0u, 1,
					"overlap: below the window");
			bad += hz_whyExpect(ovs == 0u, 1, "overlap: clears the size when it finds nothing");
		}
	}

	/* and the block must still be intact -- every case above restored its field. */
	bad += hz_whyExpect(malloc_chunkValidWhy(c, h), 0, "block restored");
	phx_free(p);

	printf("selftest why: %d wrong code(s)\n", bad);
	return bad;
}


/* ------------------------------------------------------------------ */
/* Fault-injection experiments: does corrupting a chunk header/footer
 * reproduce the STK crash signature (fault in the :346 forward-join loop at a
 * page-aligned address)?  This discriminates "allocator bug" from "caller heap
 * overflow", which is the question the crash actually poses. */


/* Can the widened page poison actually FAIL?
 *
 * The instrument used to poison one word per 4 KiB page, at +4, so it could only
 * ever detect corruption at that offset -- and "C1 always lands at page+4" was an
 * artefact of where we looked. It now samples four offsets. Before any conclusion
 * is drawn from "only +4 fired on hardware", each probe has to be shown capable of
 * firing at all: a bounds-check that quietly excluded the three new offsets would
 * produce exactly the same evidence as a genuinely offset-specific writer.
 *
 * So corrupt each probe in turn, in its own alloc/free/corrupt/alloc cycle, and
 * require the verifier to name that offset. */
static int hz_p4Probes(void)
{
	static const unsigned long offs[] = { 4u, 0x404u, 0x804u, 0xc04u };
	size_t i;
	int bad = 0;

	hz_reset();
	printf("\n--- page-poison probes: can each offset fail? ---\n");
	for (i = 0; i < sizeof(offs) / sizeof(offs[0]); i++) {
		unsigned char *b = phx_malloc(64u * 1024u);
		void *keep;
		uintptr_t page;
		void *again;

		/* Hold a live block allocated AFTER b, so freeing b leaves its chunk on a
		 * free list without the heap being munmap()ed under us -- otherwise the
		 * write below faults and takes the harness down via the SIGSEGV handler
		 * (_exit(3), which also discards buffered stdout and hides where it died). */
		keep = phx_malloc(64u);
		if ((b == NULL) || (keep == NULL)) {
			printf("  probe +0x%-5lx -> setup failed (malloc)\n", offs[i]);
			bad++;
			continue;
		}
		phx_free(b);   /* arms the poison across this chunk's pages */

		page = ((uintptr_t)b + 4095u) & ~(uintptr_t)4095u;
		/* The write under test: four bytes, exactly as hardware shows it. */
		*(volatile uint32_t *)(page + offs[i]) = 0x80000001u;

		printf("  probe +0x%-5lx : corrupted %p -- verifier must report p4off = 0x%lx\n",
			offs[i], (void *)(page + offs[i]), offs[i]);
		fflush(stdout);

		again = phx_malloc(64u * 1024u);   /* takes the chunk back -> runs the verify */
		if (again != NULL) {
			phx_free(again);
		}
		phx_free(keep);
	}
	printf("  (a probe with no 'PAGE POISON BROKEN / p4off' line above is BLIND)\n");
	return bad;
}

static void hz_experiment(const char *what)
{
	void *a, *b, *c;
	chunk_t *cb;

	hz_reset();
	hz_violation = HZ_OK;
	hz_jmpArmed = 1;

	if (sigsetjmp(hz_jmp, 1) != 0) {
		printf("experiment %-14s -> %s : %s\n", what, hz_vname[hz_violation], hz_vdetail);
		hz_jmpArmed = 0;
		return;
	}

	/* Three neighbours in one heap: a | b | c. */
	if (strcmp(what, "stk-signature") == 0) {
		a = phx_malloc(1024);
		b = phx_malloc(64);
		c = phx_malloc(64);
	}
	else {
		a = phx_malloc(64);
		b = phx_malloc(64);
		c = phx_malloc(64);
	}
	if ((a == NULL) || (b == NULL) || (c == NULL)) {
		printf("experiment %-14s -> setup failed\n", what);
		hz_jmpArmed = 0;
		return;
	}
	cb = (chunk_t *)((uintptr_t)b - CHUNK_OVERHEAD);

	if (strcmp(what, "footer") == 0) {
		/* Free `a` so that b's PUSED is clear, then smash the footer that
		 * malloc_chunkPrev(:155) reads. */
		phx_free(a);
		*((size_t *)cb - 1) = 0x1000; /* bogus prevSize -> bogus `sibling` */
		phx_free(b);
	}
	else if (strcmp(what, "nextheader") == 0) {
		/* A heap overflow out of `b` lands on c's header: exactly the scenario
		 * the comment at malloc_dl.c:203 describes. */
		memset((void *)((uintptr_t)b + 64), 0x41, 64);
		phx_free(b);
	}
	else if (strcmp(what, "hi32-write") == 0) {
		/* Reproduce the FIELD STATE seen on hardware (gate-stk, 2026-09-22): a
		 * four-byte write at heap+4 leaves heap->size with a legal low half and
		 * garbage in the high half (0x80000000_0000d000 / 0x80000001_0000d000).
		 *
		 * This does NOT reproduce the writer -- that lives outside the allocator
		 * -- it reproduces the state, which is the only thing the new
		 * hlo32/hhi32/hfixed report has to describe correctly. Without this the
		 * next real occurrence would be the first time that code ever ran. */
		heap_t *h = cb->heap;
		size_t before = h->size;

		((uint32_t *)h)[1] = 0x80000001u; /* bytes 4..7 == the high half, LE */
		printf("  injected at heap+4: %p size %#zx -> %#zx (expect hlo32=%#zx hhi32=0x80000001 hfixed=1)\n",
			(void *)h, before, h->size, (size_t)(before & 0xffffffffu));
		phx_free(b);
	}
	else if (strcmp(what, "orphan-uaf") == 0) {
		/* The counter-hypothesis, made real: force the exact bug the task
		 * suspects -- a chunk left in a GLOBAL bin while its heap is
		 * munmap()ed -- and see what the resulting fault looks like.
		 *
		 * It cannot look like the STK crash: the orphan itself lives in the
		 * dead page, so the first fault is the load of `chunk->size`, and
		 * chunks sit at heap + 16 + k, never at heap + 0.  A use-after-unmap
		 * therefore faults at a NON-page-aligned address, in
		 * _malloc_chunkAdd/_malloc_chunkRemove/malloc_chunkCanSplit -- not at a
		 * page-aligned address inside malloc_chunkIsLast. */
		chunk_t *fake;
		heap_t *h;
		void *big = phx_malloc(3000);

		phx_free(a);
		phx_free(b);
		phx_free(c);
		fake = (chunk_t *)((uintptr_t)big - CHUNK_OVERHEAD);
		h = fake->heap;
		fake = (chunk_t *)((uintptr_t)h + 2048);
		malloc_chunkInit(fake, h, 64);
		fake->size |= CHUNK_PUSED;
		hz_checkUnmap = 0;
		_malloc_chunkAdd(fake);
		phx_free(big); /* heap "fully free" -> munmap; `fake` is orphaned */
		hz_checkUnmap = 1;
		printf("  orphan %p left in sbin, its heap %p is now unmapped\n",
				(void *)fake, (void *)h);
		/* Touch the orphan the way _malloc_chunkAdd/_malloc_chunkRemove and
		 * malloc_chunkCanSplit do -- a load of chunk->size -- WITHOUT allocating
		 * first (a new heap would very likely be handed the same address back by
		 * the kernel, which is exactly why this bug class is flaky on target). */
		printf("  reading the orphan's ->size at %p (offset 0x%zx into its heap)\n",
				(void *)fake, (size_t)((uintptr_t)fake - (uintptr_t)h));
		fflush(stdout);
		printf("  orphan->size = %zu\n", *(volatile size_t *)&fake->size);
	}
	else if (strcmp(what, "stk-signature") == 0) {
		/* Reproduce the SuperTuxKart signature exactly: a READ fault at a
		 * page-aligned address inside the FORWARD-join loop (malloc_dl.c:346),
		 * with malloc_chunkIsLast (:149) inlined.
		 *
		 * The key realisation is that malloc_chunkIsFirst() (:143) compares
		 * `chunk->heap->space` -- a FLEXIBLE ARRAY MEMBER, so it is just the
		 * address heap+16 -- against `chunk`.  It never loads THROUGH ->heap.
		 * So the backward-join loop can promote a bogus `sibling` whose ->heap
		 * field is page-aligned garbage without faulting; the first actual
		 * dereference of ->heap is malloc_chunkIsLast() in the forward loop.
		 *
		 * Layout: a(1040 used) | b(80 used) | tail(free).  Free a, then smash
		 * a's FOOTER (which is what a caller's heap overflow out of `a` hits
		 * first) so that malloc_chunkPrev(b) yields a forged chunk header we
		 * planted inside a's payload, carrying heap = 0x0ceef000. */
		chunk_t *forged;
		size_t prevSize;

		phx_free(a);
		phx_free(c);

		forged = (chunk_t *)((uintptr_t)cb - 528);
		prevSize = 528;

		forged->size = 64 | CHUNK_PUSED;
		forged->heap = (heap_t *)(uintptr_t)0x0ceef000; /* far from the STK crash */
		forged->next = NULL;
		forged->prev = NULL;
		*((size_t *)cb - 1) = prevSize; /* the smashed footer */

		phx_free(b);
	}
	else if (strcmp(what, "heapptr") == 0) {
		/* Overflow clobbers c->heap with a plausible page-aligned value. */
		chunk_t *cc = (chunk_t *)((uintptr_t)c - CHUNK_OVERHEAD);

		cc->heap = (heap_t *)(uintptr_t)0x0ceef000;
		phx_free(c);
	}

	hz_checkAll();
	printf("experiment %-14s -> %s : %s\n", what,
			hz_vname[hz_violation], (hz_vdetail[0] != '\0') ? hz_vdetail : "(survived)");
	hz_jmpArmed = 0;
}


/* ------------------------------------------------------------------ */

static void hz_edgeCases(void)
{
	void *p;

	hz_reset();
	hz_violation = HZ_OK;

	p = phx_malloc(0);
	printf("malloc(0)             -> %p\n", p);
	phx_free(p);
	hz_checkAll();

	p = phx_malloc(SIZE_MAX);
	printf("malloc(SIZE_MAX)      -> %p (errno %d)\n", p, errno);
	p = phx_malloc(SIZE_MAX - 8);
	printf("malloc(SIZE_MAX-8)    -> %p (errno %d)\n", p, errno);
	/* CEIL(sizeof(heap_t)+size, _PAGE_SIZE) overflow guard, malloc_dl.c:372 */
	p = phx_malloc(SIZE_MAX - 4096);
	printf("malloc(SIZE_MAX-4096) -> %p (errno %d)\n", p, errno);
	p = phx_calloc(SIZE_MAX / 2, 4);
	printf("calloc(SIZE_MAX/2,4)  -> %p (errno %d)\n", p, errno);
	hz_checkAll();
	printf("edge cases            -> %s\n", hz_vname[hz_violation]);
}


int main(int argc, char **argv)
{
	struct sigaction sa;
	int nops = 100000;
	int nseeds = 8;
	uint64_t seed0 = 1;
	int doShrink = 1;
	int doExp = 1;
	int doSelf = 1;
	int rawSegv = 0;
	const char *onlyExp = NULL;
	unsigned int mtThreads = 0;
	unsigned long mtOps = 50000ul;
	int i, s, rc = 0;

	for (i = 1; i < argc; i++) {
		if ((strcmp(argv[i], "--ops") == 0) && (i + 1 < argc)) {
			nops = atoi(argv[++i]);
		}
		else if ((strcmp(argv[i], "--seeds") == 0) && (i + 1 < argc)) {
			nseeds = atoi(argv[++i]);
		}
		else if ((strcmp(argv[i], "--seed0") == 0) && (i + 1 < argc)) {
			seed0 = strtoull(argv[++i], NULL, 0);
		}
		else if ((strcmp(argv[i], "--check-every") == 0) && (i + 1 < argc)) {
			hz_checkEvery = atoi(argv[++i]);
		}
		else if (strcmp(argv[i], "--no-shrink") == 0) {
			doShrink = 0;
		}
		else if (strcmp(argv[i], "--no-experiments") == 0) {
			doExp = 0;
		}
		else if (strcmp(argv[i], "--no-selftest") == 0) {
			doSelf = 0;
		}
		else if (strcmp(argv[i], "--raw-segv") == 0) {
			rawSegv = 1;
		}
		else if ((strcmp(argv[i], "--threads") == 0) && (i + 1 < argc)) {
			mtThreads = (unsigned int)atoi(argv[++i]);
		}
		else if ((strcmp(argv[i], "--mt-ops") == 0) && (i + 1 < argc)) {
			mtOps = strtoul(argv[++i], NULL, 0);
		}
		else if ((strcmp(argv[i], "--exp") == 0) && (i + 1 < argc)) {
			onlyExp = argv[++i];
		}
		else if ((strcmp(argv[i], "--fragile") == 0) && (i + 1 < argc)) {
			hz_fragile = atoi(argv[++i]);
		}
		else {
			fprintf(stderr, "usage: %s [--ops N] [--seeds N] [--seed0 S] "
					"[--check-every N] [--no-shrink] [--no-experiments] [--no-selftest]\n", argv[0]);
			return 2;
		}
	}
	if (nops > HZ_MAX_OPS) {
		nops = HZ_MAX_OPS;
	}

	if (rawSegv == 0) {
		memset(&sa, 0, sizeof(sa));
		sa.sa_sigaction = hz_segv;
		sa.sa_flags = SA_SIGINFO;
		sigaction(SIGSEGV, &sa, NULL);
		sigaction(SIGBUS, &sa, NULL);
	}

	printf("CHUNK_OVERHEAD=%zu CHUNK_MIN_SIZE=%zu CHUNK_SMALLBIN_MAX_SIZE=%zu "
			"sizeof(heap_t)=%zu sizeof(chunk_t)=%zu _PAGE_SIZE=%lu\n",
			(size_t)CHUNK_OVERHEAD, (size_t)CHUNK_MIN_SIZE, (size_t)CHUNK_SMALLBIN_MAX_SIZE,
			sizeof(heap_t), sizeof(chunk_t), (unsigned long)_PAGE_SIZE);

	if (onlyExp != NULL) {
		hz_experiment(onlyExp);
		return 0;
	}

	if (doSelf != 0) {
		printf("\n--- malloc_chunkValidWhy() code coverage ---\n");
		if (hz_whySelftest() != 0) {
			fprintf(stderr, "harness: the corrupt-header report would name the wrong "
					"check; a field fire is read through those codes\n");
			return 2;
		}
		printf("\n--- detector self-test ---\n");
		if (hz_selftest() != 0) {
			fprintf(stderr, "harness: the checker cannot see the bug it is hunting; "
					"a clean stress run would be meaningless\n");
			return 2;
		}
	}

	if (mtThreads > 1u) {
		printf("\n--- multithreaded stress (%u threads) ---\n", mtThreads);
		if (hz_mtStress(mtThreads, mtOps) != 0) {
			fprintf(stderr, "harness: MULTITHREADED stress found a problem\n");
			return 3;
		}
	}

	printf("\n--- randomized stress ---\n");
	for (s = 0; s < nseeds; s++) {
		uint64_t seed = seed0 + (uint64_t)s * 0x9e3779b9u;
		int v;

		hz_gen(seed, nops);
		v = hz_run(hz_ops, hz_nops);
		printf("seed 0x%016llx  ops %6d  check-every %d  -> %s%s%s\n",
				(unsigned long long)seed, hz_nops, hz_checkEvery, hz_vname[v],
				(v != HZ_OK) ? " @op " : "",
				(v != HZ_OK) ? "" : "");
		printf("    coverage: mmap=%lu munmap=%lu checks=%lu chunksWalked=%lu freeChunks=%lu\n"
				"              realloc shrink-split=%lu grow-in-place=%lu | join back=%lu fwd=%lu\n"
				"              max live heaps=%d  max chunks in one heap=%d  max binned chunks=%d\n",
				hz_cov.mmaps, hz_cov.munmaps, hz_cov.checks, hz_cov.chunksWalked,
				hz_cov.freeChunksSeen, hz_cov.reallocShrinkSplit, hz_cov.reallocGrowInPlace,
				hz_cov.joinBackward, hz_cov.joinForward,
				hz_cov.maxLiveHeaps, hz_cov.maxChunksInHeap, hz_cov.maxBinChunks);
		memset(&hz_cov, 0, sizeof(hz_cov));
		if (v != HZ_OK) {
			printf("  at op %ld: %s\n", hz_vop, hz_vdetail);
			rc = 1;
			if (doShrink != 0) {
				hz_shrink(v, hz_nops);
			}
			break;
		}
		fflush(stdout);
	}

	if (doExp != 0) {
		printf("\n--- edge cases ---\n");
		hz_edgeCases();
		(void)hz_p4Probes();
		printf("\n--- fault injection (is the STK signature an allocator bug or a caller overflow?) ---\n");
		hz_experiment("footer");
		hz_experiment("nextheader");
		hz_experiment("heapptr");
	hz_experiment("stk-signature");
	hz_experiment("orphan-uaf");
	hz_experiment("hi32-write");
	}

	return rc;
}
