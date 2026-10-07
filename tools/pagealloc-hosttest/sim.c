/*
 * Host simulation of the kernel page allocator (vm/page.c, compiled in unchanged) under the
 * allocation pattern of the build 48 gate: a process that allocates and touches nearly all free
 * memory in 64 MiB pieces while other processes run, then exits; then a browser-like phase of
 * anonymous memory churn plus GPU buffers (physically contiguous blocks of 64 KiB .. 4 MiB).
 *
 * Every user page costs the kernel what it costs on the Pi (aarch64, 4 KiB pages):
 *   - an anon_t (104 B -> a 128 B kmalloc block, 32 per kmalloc zone page),
 *   - a level 3 page table per 2 MiB of virtual space touched (kept until the process exits),
 *   - the amap array of a mapping (8 B per page + header -> a 256 KiB kmalloc block per 64 MiB).
 * kmalloc is modelled as vm/kmalloc.c does it: a zone page per 32 blocks, allocation from the
 * first zone with a free block, a zone that gets a block back goes to the tail of the list, an
 * empty zone gives its page back.
 *
 * Build: ./sync.sh <name> <kernel tree> copies vm/page.c into src-<name>/; make builds sim-<name>
 *        for every src-<name> (here: old = master 4f85ec30, fix = + coalescing and hole fixes,
 *        grp = + grouping by mobility).
 * Modes: fuzz     random sizes and kinds to exhaustion and back, free-list invariants each round
 *        coalesce all memory as single pages, freed in random order: must merge back completely
 *        gate     the build 48 gate model (objcache memory_pressure, then a browser load)
 *        testa    mem/test_frag after_big_process; pinner: beside_kernel_pages_of_another
 * Env:   PINS=1 classifies what pins each 4 MiB region; NOPROBE=1 skips the capacity probes.
 * Run:   ./sim-<x> [fuzz|coalesce|gate|testa|pinner] [seed] [faults of the big process per daemon step, 0: none]
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "page.c"


static size_t checkLists(void);


/* Physical memory of the Pi 4 (4 GB) as the b48 boot log draws it: [0, 0x38000000) with the
 * kernel and the boot reserved pages at the bottom and a reserved 2 MB window inside, a 128 MB
 * hole, then [0x40000000, 0xfc000000) */
static const struct {
	addr_t start, end;
} banks[] = { { 0x0, 0x38000000 }, { 0x40000000, 0xfc000000 } };

#define KERNEL_END  0x02402000UL /* 9218 pages: kernel image, page_t array, map entry pool */
#define RESV_START  0x2ee00000UL
#define RESV_END    0x2f000000UL


int pmap_getPage(page_t *page, addr_t *addr)
{
	addr_t a = *addr & ~(SIZE_PAGE - 1U);
	unsigned int i;

	for (i = 0; i < sizeof(banks) / sizeof(banks[0]); i++) {
		if (a < banks[i].start) {
			a = banks[i].start;
		}
		if (a < banks[i].end) {
			break;
		}
	}
	if (i == sizeof(banks) / sizeof(banks[0])) {
		return -ENOMEM;
	}

	page->addr = a;
	if (a < KERNEL_END) {
		page->flags = PAGE_OWNER_KERNEL;
	}
	else if ((a >= RESV_START) && (a < RESV_END)) {
		page->flags = PAGE_OWNER_BOOT;
	}
	else {
		page->flags = PAGE_FREE;
	}
	*addr = a + SIZE_PAGE;

	return EOK;
}


char pmap_marker(page_t *p)
{
	if ((p->flags & PAGE_FREE) != 0U) {
		return '.';
	}
	return ((p->flags & PAGE_OWNER_APP) != 0U) ? 'A' : 'K';
}


size_t vm_objectCachedPages(void)
{
	return 0;
}


static int (*reclaimHook)(void);


int vm_objectReclaim(void)
{
	return (reclaimHook != NULL) ? reclaimHook() : 0;
}


int vm_objectReclaimLow(size_t freesz)
{
	(void)freesz;
	return 0;
}


/* ---- random numbers (deterministic) ---- */

static unsigned long long rngState = 88172645463325252ULL;


static unsigned long long rnd(void)
{
	rngState ^= rngState << 13;
	rngState ^= rngState >> 7;
	rngState ^= rngState << 17;
	return rngState;
}


static unsigned long rndn(unsigned long n)
{
	return (unsigned long)(rnd() % n);
}


/* ---- kmalloc model: 128 B blocks (anon_t), 32 per zone page ---- */

typedef struct _zone_t {
	page_t *pg;
	unsigned int used;
	struct _zone_t *next, *prev;
	int listed;
} zone_t;

#define ZONE_BLOCKS 32U

static struct {
	zone_t *partial; /* zones with a free block, allocation from the head */
	size_t zones;
} km;


static zone_t *anonAlloc(void)
{
	zone_t *z = km.partial;

	if (z == NULL) {
		z = calloc(1, sizeof(*z));
		z->pg = vm_pageAlloc(SIZE_PAGE, PAGE_OWNER_KERNEL | PAGE_KERNEL_HEAP);
		if (z->pg == NULL) {
			free(z);
			return NULL;
		}
		km.zones++;
		LIST_ADD(&km.partial, z);
		z->listed = 1;
	}

	if (++z->used == ZONE_BLOCKS) {
		LIST_REMOVE(&km.partial, z);
		z->listed = 0;
	}

	return z;
}


static void anonFree(zone_t *z)
{
	if (z->used-- == ZONE_BLOCKS) {
		LIST_ADD(&km.partial, z);
		z->listed = 1;
	}
	if (z->used == 0U) {
		LIST_REMOVE(&km.partial, z);
		vm_pageFree(z->pg);
		free(z);
		km.zones--;
	}
}


/* ---- processes ---- */

typedef struct {
	size_t n, cap;
	page_t **pages; /* NULL once unmapped */
	zone_t **anons;
	page_t *amap;   /* the amap array (a kmalloc block of its own above a page) */
	size_t touched;
} chunk_t;

typedef struct {
	chunk_t *chunks;
	size_t nchunks, capchunks;
	page_t **ptables; /* level 3 tables, until exit */
	size_t nptables, capptables;
	size_t vaPages;   /* virtual pages used so far: a new table at each 512 */
	size_t resident;
	int alive;
} proc_t;


static void push(void ***arr, size_t *n, size_t *cap, void *v)
{
	if (*n == *cap) {
		*cap = (*cap == 0U) ? 64U : (*cap * 2U);
		*arr = realloc(*arr, *cap * sizeof(void *));
	}
	(*arr)[(*n)++] = v;
}


static int procMap(proc_t *p, size_t npages)
{
	chunk_t *c;
	size_t amapsz = 64U + npages * 8U;

	if (p->nchunks == p->capchunks) {
		p->capchunks = (p->capchunks == 0U) ? 16U : (p->capchunks * 2U);
		p->chunks = realloc(p->chunks, p->capchunks * sizeof(chunk_t));
	}
	c = &p->chunks[p->nchunks];
	memset(c, 0, sizeof(*c));
	c->n = npages;
	c->pages = calloc(npages, sizeof(page_t *));
	c->anons = calloc(npages, sizeof(zone_t *));
	/* Small amaps live in shared kmalloc zones, not modelled; a big one is a block of its own */
	if (amapsz > SIZE_PAGE) {
		c->amap = vm_pageAlloc(amapsz, PAGE_OWNER_KERNEL | PAGE_KERNEL_HEAP);
		if (c->amap == NULL) {
			free(c->pages);
			free(c->anons);
			return -1;
		}
	}
	p->nchunks++;
	return (int)(p->nchunks - 1U);
}


/* A fault on page i of chunk ci: the page, its anon_t, maybe a page table */
static int procTouch(proc_t *p, size_t ci, size_t i)
{
	chunk_t *c = &p->chunks[ci];
	page_t *pg;
	zone_t *z;

	if (c->pages[i] != NULL) {
		return 0;
	}
	pg = vm_pageAlloc(SIZE_PAGE, PAGE_OWNER_APP);
	if (pg == NULL) {
		return -1;
	}
	z = anonAlloc();
	if (z == NULL) {
		vm_pageFree(pg);
		return -1;
	}
	if ((p->vaPages++ % 512U) == 0U) {
		page_t *pt = vm_pageAlloc(SIZE_PAGE, PAGE_OWNER_KERNEL | PAGE_KERNEL_PTABLE);
		if (pt == NULL) {
			anonFree(z);
			vm_pageFree(pg);
			return -1;
		}
		push((void ***)&p->ptables, &p->nptables, &p->capptables, pt);
	}
	c->pages[i] = pg;
	c->anons[i] = z;
	c->touched++;
	p->resident++;
	return 0;
}


static void procUnmapPage(proc_t *p, size_t ci, size_t i)
{
	chunk_t *c = &p->chunks[ci];

	if (c->pages[i] != NULL) {
		vm_pageFree(c->pages[i]);
		anonFree(c->anons[i]);
		c->pages[i] = NULL;
		c->anons[i] = NULL;
		c->touched--;
		p->resident--;
	}
}


static void procUnmap(proc_t *p, size_t ci)
{
	chunk_t *c = &p->chunks[ci];
	size_t i;

	for (i = 0; i < c->n; i++) {
		procUnmapPage(p, ci, i);
	}
	if (c->amap != NULL) {
		vm_pageFree(c->amap);
		c->amap = NULL;
	}
	free(c->pages);
	free(c->anons);
	c->pages = NULL;
	c->anons = NULL;
	c->n = 0;
}


static void procExit(proc_t *p)
{
	size_t i;

	for (i = 0; i < p->nchunks; i++) {
		if (p->chunks[i].pages != NULL) {
			procUnmap(p, i);
		}
	}
	for (i = 0; i < p->nptables; i++) {
		vm_pageFree(p->ptables[i]);
	}
	free(p->chunks);
	free(p->ptables);
	memset(p, 0, sizeof(*p));
}


/* ---- measurements ---- */

static size_t freeBytes(void)
{
	return pages_info.totalsz - pages_info.allocsz;
}


/* The largest naturally aligned, physically contiguous, free block (allocator-independent) */
static size_t largestFree(void)
{
	size_t n = pages_info.totalsz / SIZE_PAGE, i, run = 0, best = 0, blk;
	unsigned int k;

	for (i = n; i-- > 0;) {
		page_t *p = &pages_info.pages[i];
		if ((p->flags & PAGE_FREE) == 0U) {
			run = 0;
			continue;
		}
		if ((i + 1U < n) && (run != 0U) && (pages_info.pages[i + 1U].addr != p->addr + SIZE_PAGE)) {
			run = 0;
		}
		run++;
		k = hal_cpuGetFirstBit(p->addr);
		blk = (k >= 63U) ? ((size_t)1 << 62) : ((size_t)1 << k);
		while (blk > run * SIZE_PAGE) {
			blk >>= 1;
		}
		best = (blk > best) ? blk : best;
	}
	return best;
}


/* How many blocks of sz the allocator hands out (up to max), all given back afterwards */
static size_t capacity(size_t sz, size_t maxn)
{
	page_t **got = malloc(maxn * sizeof(page_t *));
	size_t n = 0, i;

	while (n < maxn) {
		got[n] = vm_pageAlloc(sz, PAGE_OWNER_APP);
		if (got[n] == NULL) {
			break;
		}
		n++;
	}
	for (i = 0; i < n; i++) {
		vm_pageFree(got[i]);
	}
	free(got);
	return n;
}


/* What keeps 4 MiB regions (aligned) from being free: kernel pages only, user pages only, both */
static void pins(const char *what)
{
	size_t n = pages_info.totalsz / SIZE_PAGE, i, k = 0, a = 0, ka = 0, fr = 0, kpages = 0;
	addr_t region = (addr_t)-1;
	int hasK = 0, hasA = 0, any = 0;

	for (i = 0; i <= n; i++) {
		page_t *p = (i < n) ? &pages_info.pages[i] : NULL;
		addr_t r = (p != NULL) ? (p->addr >> 22) : (addr_t)-2;
		if (r != region) {
			if (region != (addr_t)-1) {
				if (hasK && hasA) ka++;
				else if (hasK) k++;
				else if (hasA) a++;
				else if (any) fr++;
			}
			region = r;
			hasK = hasA = 0;
			any = 0;
		}
		if (p == NULL) {
			break;
		}
		any = 1;
		if ((p->flags & PAGE_FREE) == 0U) {
			if (p->addr < KERNEL_END || (p->addr >= RESV_START && p->addr < RESV_END)) {
				hasK = 1;
			}
			else if ((p->flags & PAGE_OWNER_APP) != 0U) {
				hasA = 1;
			}
			else {
				hasK = 1;
				kpages++;
			}
		}
	}
	printf("  pins %-28s 4 MiB regions: free %zu, kernel only %zu, user only %zu, both %zu (kernel pages %zu)\n", what, fr, k, a, ka, kpages);
}


static void report(const char *what)
{
	if (getenv("PINS") != NULL) {
		pins(what);
	}
	size_t bad = checkLists();
	if (bad != 0U) {
		printf("%zu INVARIANT VIOLATIONS\n", bad);
	}
	/* NOPROBE: no capacity probes (they allocate and free, which changes what follows) */
	if (getenv("NOPROBE") != NULL) {
		printf("%-44s free %5zu MiB  largest free block %8zu KiB  zones %zu\n", what, freeBytes() >> 20, largestFree() >> 10, km.zones);
		return;
	}
	printf("%-44s free %5zu MiB  largest free block %8zu KiB  4 MiB blocks %4zu  16 MiB blocks %3zu  zones %zu\n", what,
		freeBytes() >> 20, largestFree() >> 10, capacity(4UL << 20, 4096), capacity(16UL << 20, 1024), km.zones);
}


/* The free lists' invariants; returns the number of violations (and prints the first ones):
 * a block is on the list of its size, physically contiguous, its pages are free; with grouping
 * (PAGE_LISTS) it is on the list of its pageblock's kind (shared at and above a pageblock) and
 * every page of that pageblock is of one kind; and the blocks add up to the free memory */
static size_t checkLists(void)
{
#ifdef PAGE_LISTS
	page_t **lists = &pages_info.sizes[0][0];
	size_t nlists = PAGE_LISTS * SIZE_VM_SIZES;
#else
	page_t **lists = pages_info.sizes;
	size_t nlists = SIZE_VM_SIZES;
#endif
	size_t l, bad = 0, j, npg = pages_info.totalsz / SIZE_PAGE, freepg = 0, pi;
	page_t *p;

#define BAD(...) do { if (bad++ < 8U) { printf("invariant: " __VA_ARGS__); } } while (0)
	for (l = 0; l < nlists; l++) {
		p = lists[l];
		if (p == NULL) {
			continue;
		}
		do {
			unsigned int idx = (unsigned int)(l % SIZE_VM_SIZES);
			size_t cnt = ((size_t)1 << p->idx) / SIZE_PAGE;
			pi = (size_t)(p - pages_info.pages);
			if (p->idx != idx) {
				BAD("block %#llx idx %u on list of %u\n", (unsigned long long)p->addr, p->idx, idx);
			}
			if ((pi + cnt > npg) || (p[cnt - 1U].addr != p->addr + (cnt - 1U) * SIZE_PAGE) || ((p->addr & (((addr_t)1 << p->idx) - 1U)) != 0U)) {
				BAD("block %#llx idx %u not contiguous or not aligned\n", (unsigned long long)p->addr, p->idx);
			}
			else {
				for (j = 0; j < cnt; j++) {
					if ((p[j].flags & PAGE_FREE) == 0U) {
						BAD("block %#llx idx %u has a used page\n", (unsigned long long)p->addr, p->idx);
						break;
					}
				}
			}
			freepg += cnt;
#ifdef PAGE_LISTS
			{
				unsigned int kind = (unsigned int)(l / SIZE_VM_SIZES);
				unsigned int want = (idx >= pages_info.blockidx) ? PAGE_LIST_BLOCKS :
					(((p->flags & PAGE_BLOCK_KERNEL) != 0U) ? PAGE_LIST_KERNEL : PAGE_LIST_APP);
				if (kind != want) {
					BAD("block %#llx idx %u on list kind %u, its kind %u\n", (unsigned long long)p->addr, idx, kind, want);
				}
				if (idx < pages_info.blockidx) {
					addr_t base = p->addr & ~((addr_t)VM_PAGEBLOCK_PAGES * SIZE_PAGE - 1U);
					size_t q = pi;
					while ((q > 0U) && (pages_info.pages[q - 1U].addr >= base)) {
						q--;
					}
					for (; (q < npg) && (pages_info.pages[q].addr < base + VM_PAGEBLOCK_PAGES * SIZE_PAGE); q++) {
						if ((pages_info.pages[q].flags & PAGE_BLOCK_KERNEL) != (p->flags & PAGE_BLOCK_KERNEL)) {
							BAD("pageblock %#llx of mixed kind (page %#llx)\n", (unsigned long long)base, (unsigned long long)pages_info.pages[q].addr);
							break;
						}
					}
				}
			}
#endif
			p = p->next;
		} while (p != lists[l]);
	}
	if (freepg * SIZE_PAGE != pages_info.totalsz - pages_info.allocsz) {
		BAD("free lists hold %zu pages, %zu free\n", freepg, (pages_info.totalsz - pages_info.allocsz) / SIZE_PAGE);
	}
#undef BAD
	return bad;
}


/* Random sizes (4 KiB .. 8 MiB) and kinds, to exhaustion and back, invariants after every round */
static int fuzz(unsigned int rounds)
{
	enum { MAXB = 1 << 20 };
	static page_t *held[MAXB];
	static const vm_flags_t kinds[] = { PAGE_OWNER_APP, PAGE_OWNER_KERNEL | PAGE_KERNEL_HEAP, PAGE_OWNER_KERNEL | PAGE_KERNEL_PTABLE };
	size_t n = 0, i, bad = 0, fails = 0;
	unsigned int r;

	for (r = 0; r < rounds; r++) {
		/* Fill up: mostly single pages, some blocks of up to 8 MiB */
		while (n < MAXB) {
			size_t sz = (rndn(8) != 0U) ? SIZE_PAGE : (SIZE_PAGE << rndn(12));
			page_t *p = vm_pageAlloc(sz, kinds[rndn(3)]);
			if (p == NULL) {
				if (++fails > 64U) {
					break;
				}
				continue;
			}
			held[n++] = p;
		}
		fails = 0;
		bad += checkLists();
		/* Give back a random part (all of it every 4th round) */
		for (i = 0; i < n;) {
			if (((r % 4U) == 3U) || (rndn(100) < 60U)) {
				vm_pageFree(held[i]);
				held[i] = held[--n];
			}
			else {
				i++;
			}
		}
		bad += checkLists();
	}
	for (i = 0; i < n; i++) {
		vm_pageFree(held[i]);
	}
	bad += checkLists();
	report("fuzz: everything given back");
	printf("fuzz: %u rounds, %zu invariant violations\n", rounds, bad);
	return (bad == 0U) ? 0 : 1;
}


/* ---- workloads ---- */

#define NDAEMONS 16
static proc_t daemons[NDAEMONS];


/* A daemon does something small: touches a page, or unmaps one, or maps a new small chunk */
static void daemonStep(void)
{
	proc_t *d = &daemons[rndn(NDAEMONS)];
	size_t ci, i;

	if ((d->nchunks == 0U) || (rndn(64) == 0U)) {
		(void)procMap(d, 16U + rndn(240));
		return;
	}
	ci = rndn(d->nchunks);
	if (d->chunks[ci].pages == NULL) {
		return;
	}
	i = rndn(d->chunks[ci].n);
	/* Working sets grow slowly: a touch is more likely than an unmap */
	if (rndn(100) < 50U) {
		(void)procTouch(d, ci, i);
	}
	else {
		procUnmapPage(d, ci, i);
	}
}


static void bootDaemons(size_t steps)
{
	size_t s;

	for (s = 0; s < steps; s++) {
		daemonStep();
	}
}


/* The page cache of the objcache test: a 512 MiB file read in 16-page clusters, given up in one
 * piece when memory runs out (vm_objectReclaim() frees one object per call) */
static page_t **cachePages;
static size_t ncache;


static int reclaimCache(void)
{
	size_t i, n = ncache;

	for (i = 0; i < ncache; i++) {
		vm_pageFree(cachePages[i]);
	}
	ncache = 0;
	return (n != 0U) ? 1 : 0;
}


/* A process that maps and touches len bytes in 64 MiB pieces, a daemon step every 'every' faults */
static int bigChild(proc_t *c, size_t len, unsigned int every, void (*between)(size_t))
{
	size_t done = 0, chunk, i, faults = 0;
	int ci;

	while (done < len) {
		chunk = ((len - done) < (64UL << 20)) ? (len - done) : (64UL << 20);
		ci = procMap(c, chunk / SIZE_PAGE);
		if (ci < 0) {
			return -1;
		}
		for (i = 0; i < chunk / SIZE_PAGE; i++) {
			if (procTouch(c, (size_t)ci, i) < 0) {
				return -1;
			}
			if ((every != 0U) && ((++faults % every) == 0U)) {
				daemonStep();
			}
			if ((between != NULL) && (((done / SIZE_PAGE) + i + 1U) % 512U == 0U)) {
				between((done / SIZE_PAGE) + i + 1U);
			}
		}
		done += chunk;
	}
	return 0;
}


/* The browser phase: a few processes whose anonymous memory churns within a working set (the
 * web process 700 MiB, the UI 300, network 150, compositor 100), plus GPU buffers of 64 KiB ..
 * 3.6 MiB (MAP_CONTIGUOUS through shmsrv / rpi4-v3d-async), up to 96 held at a time */
static void browserPhase(size_t steps)
{
	enum { NPROC = 4, NGPU = 96 };
	static const size_t budget[NPROC] = { 700, 300, 150, 100 };
	static proc_t bp[NPROC];
	static page_t *gpu[NGPU];
	static const size_t gpusz[] = { 64UL << 10, 256UL << 10, 1UL << 20, 3652UL << 10 };
	size_t s, fails = 0, tries = 0, i, pi;
	proc_t *p;

	for (s = 0; s < steps; s++) {
		unsigned long r = rndn(1000);
		pi = rndn(NPROC);
		p = &bp[pi];

		if (r < 6) {
			i = rndn(NGPU);
			if (gpu[i] != NULL) {
				vm_pageFree(gpu[i]);
				gpu[i] = NULL;
			}
			else {
				tries++;
				gpu[i] = vm_pageAlloc(gpusz[rndn(4)], PAGE_OWNER_APP);
				if (gpu[i] == NULL) {
					fails++;
				}
			}
		}
		else if (r < 9) {
			/* malloc arena / mmap of 64 KiB .. 8 MiB */
			(void)procMap(p, 16U + rndn(2048));
		}
		else if (((r < 12) || ((p->resident * SIZE_PAGE) > (budget[pi] << 20))) && (p->nchunks != 0U)) {
			/* munmap of a whole mapping */
			i = rndn(p->nchunks);
			if (p->chunks[i].pages != NULL) {
				procUnmap(p, i);
			}
		}
		else if (r < 13) {
			daemonStep();
		}
		else if (p->nchunks != 0U) {
			/* page faults, mostly on recent mappings */
			i = p->nchunks - 1U - rndn((p->nchunks < 8U) ? p->nchunks : 8U);
			if (p->chunks[i].pages != NULL) {
				(void)procTouch(p, i, rndn(p->chunks[i].n));
			}
		}

		if ((s + 1U) % (steps / 4U) == 0U) {
			char what[64];
			size_t res = 0;
			for (i = 0; i < NPROC; i++) {
				res += bp[i].resident;
			}
			snprintf(what, sizeof(what), "browser %zu%% (%zu MiB, gpu fails %zu/%zu)", (s + 1U) * 100U / steps,
				(res * SIZE_PAGE) >> 20, fails, tries);
			report(what);
		}
	}
}


/* The test of mem/test_frag.c: a pinner process maps one page of a file at a new 2 MiB slot of
 * its address space (a page table, nothing else) after every 2 MiB the big child touches */
static proc_t pinner;


static void pinnerStep(size_t pagesDone)
{
	(void)pagesDone;
	page_t *pt = vm_pageAlloc(SIZE_PAGE, PAGE_OWNER_KERNEL | PAGE_KERNEL_PTABLE);

	if (pt != NULL) {
		push((void ***)&pinner.ptables, &pinner.nptables, &pinner.capptables, pt);
	}
}


int main(int argc, char *argv[])
{
	static char heap[48UL << 20];
	void *bss = heap, *top = heap + sizeof(heap);
	pmap_t pmap;
	const char *mode = (argc > 1) ? argv[1] : "gate";
	unsigned int daemonEvery = (argc > 3) ? (unsigned int)strtoul(argv[3], NULL, 0) : 64U;
	proc_t child;
	size_t before, target, i;
	int err;

	if (argc > 2) {
		rngState += strtoull(argv[2], NULL, 0) * 0x9e3779b97f4a7c15ULL;
	}

	setvbuf(stdout, NULL, _IOLBF, 0);
	_page_init(&pmap, &bss, &top);
	printf("free lists after init: %zu invariant violations\n", checkLists());
	report("boot");

	bootDaemons(400000);
	report("daemons up");

	memset(&child, 0, sizeof(child));

	if (strcmp(mode, "fuzz") == 0) {
		return fuzz(40);
	}

	if (strcmp(mode, "coalesce") == 0) {
		/* Everything as single pages, given back in random order: all of it must merge again */
		size_t n = freeBytes() / SIZE_PAGE, j;
		page_t **all = malloc(n * sizeof(page_t *)), *t;
		for (i = 0; i < n; i++) {
			all[i] = vm_pageAlloc(SIZE_PAGE, PAGE_OWNER_APP);
		}
		for (i = n; i > 1; i--) {
			j = rndn(i);
			t = all[i - 1];
			all[i - 1] = all[j];
			all[j] = t;
		}
		for (i = 0; i < n; i++) {
			vm_pageFree(all[i]);
		}
		report("all pages freed in random order");
		return 0;
	}

	if (strcmp(mode, "gate") == 0) {
		/* test_objcache memory_pressure_evicts_cache: 512 MiB file cached, then a child takes all
		 * free memory but max(256 MiB, 10%) */
		ncache = (512UL << 20) / SIZE_PAGE;
		cachePages = malloc(ncache * sizeof(page_t *));
		for (i = 0; i < ncache; i++) {
			cachePages[i] = vm_pageAlloc(SIZE_PAGE, PAGE_OWNER_APP);
			if ((i % 256U) == 0U) {
				daemonStep();
			}
		}
		before = freeBytes() + ncache * SIZE_PAGE;
		target = before - ((before / 10U > (256UL << 20)) ? before / 10U : (256UL << 20));
		report("512 MiB file cached");
		reclaimHook = reclaimCache;

		err = bigChild(&child, target, daemonEvery, NULL);
		printf("child touched %zu MiB: %s\n", target >> 20, (err == 0) ? "ok" : "FAILED");
		report("child at its peak");
		procExit(&child);
		report("child exited");

		browserPhase(4000000);
	}
	else {
		/* mem/test_frag: "testa" the big child alone, "pinner" with the pinner beside it, staying */
		before = freeBytes();
		target = before - ((before / 10U > (256UL << 20)) ? before / 10U : (256UL << 20));
		err = bigChild(&child, target, 0, (strcmp(mode, "pinner") == 0) ? pinnerStep : NULL);
		printf("child touched %zu MiB: %s; pinner holds %zu page tables\n", target >> 20, (err == 0) ? "ok" : "FAILED", pinner.nptables);
		procExit(&child);
		report("child exited, pinner alive");
		{
			page_t *b16 = vm_pageAlloc(16UL << 20, PAGE_OWNER_APP);
			size_t n4 = capacity(4UL << 20, 256);
			printf("test_frag: 16 MiB block %s, 4 MiB blocks %zu of 256 (1 GiB) -> %s\n", (b16 != NULL) ? "ok" : "NONE", n4,
				((b16 != NULL) && (n4 == 256U)) ? "PASS" : "FAIL");
			vm_pageFree(b16);
		}
		procExit(&pinner);
		report("pinner exited");
	}

	return 0;
}
