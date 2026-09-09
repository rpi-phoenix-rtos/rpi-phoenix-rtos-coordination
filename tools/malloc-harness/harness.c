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
/* Fault-injection experiments: does corrupting a chunk header/footer
 * reproduce the STK crash signature (fault in the :346 forward-join loop at a
 * page-aligned address)?  This discriminates "allocator bug" from "caller heap
 * overflow", which is the question the crash actually poses. */

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
		printf("\n--- detector self-test ---\n");
		if (hz_selftest() != 0) {
			fprintf(stderr, "harness: the checker cannot see the bug it is hunting; "
					"a clean stress run would be meaningless\n");
			return 2;
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
		printf("\n--- fault injection (is the STK signature an allocator bug or a caller overflow?) ---\n");
		hz_experiment("footer");
		hz_experiment("nextheader");
		hz_experiment("heapptr");
	hz_experiment("stk-signature");
	hz_experiment("orphan-uaf");
	}

	return rc;
}
