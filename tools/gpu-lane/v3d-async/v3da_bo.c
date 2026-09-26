/*
 * Phoenix-RTOS
 *
 * Raspberry Pi 4 (BCM2711) V3D 4.2 asynchronous render server - BO manager
 *
 * M1a backing: one MAP_CONTIGUOUS block per BO, zeroed at hand-out, mapped into the
 * one GPU page table page by page (old daemon's ioc_create_bo, v3d_gpu.c:680-796),
 * shared with the client by physical address (memref V3DA_MEM_PHYS).
 *
 * What is new against the old lane:
 *   - Free is a QUARANTINE, never an immediate free: PTEs cleared -> TLB flushed ->
 *     every queue has passed the hw_submitted snapshot taken at release -> only
 *     then the block is reused (design section 9.2). No V3D job the server knows
 *     of can write a page after it changed owner.
 *   - Handles carry a per-slot generation and are never reused while the server
 *     lives (the winsys's monotonic-handle lesson).
 *   - In-flight references: every BO a submit names is pinned until the job that
 *     holds the list completes; a close while pinned only drops the handle.
 *   - SCANOUT BOs (the transitional present family): the GPU pages of the visible
 *     rows are the firmware framebuffer buffer's pages; the CPU view (memref) is
 *     the BO's own DRAM, exactly as the in-process winsys does it.
 *   - Blocks go to a server-owned POOL, not back to the kernel: a stale device or
 *     stale client MAP_PHYSMEM write then lands in another GPU buffer, never in a
 *     malloc heap (the C1 class), and the last-munmap-of-a-contiguous-object kernel
 *     bug (E1 section 6) is not exercised.
 *
 * All functions run with srv.lock held.
 *
 * Copyright 2026 Phoenix Systems
 * Author: Witold Bołt
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <sys/mman.h>

#include "v3da.h"
#include "v3da_regs.h"


/* Any LIVE BO with this exact handle (pinned-but-closed ones included). */
static v3da_bo_t *bo_lookup(uint32_t handle)
{
	uint32_t slot = handle & V3DA_HANDLE_SLOT_MASK;
	v3da_bo_t *b;

	if ((slot == 0u) || (slot > srv.nbos)) {
		return NULL;
	}
	b = &srv.bos[slot - 1u];
	return ((b->state == V3DA_BO_LIVE) && (b->handle == handle)) ? b : NULL;
}


/* A BO a client may still name: LIVE and not closed by every handle holder. */
v3da_bo_t *v3da_bo_find(uint32_t handle)
{
	v3da_bo_t *b = bo_lookup(handle);

	return ((b != NULL) && (b->refs > 0u)) ? b : NULL;
}


/* A block for `pages` pages: an exact-size pooled block of the same memory type,
 * else a fresh MAP_CONTIGUOUS mapping. Zeroed either way (Phoenix contiguous pages
 * are not zeroed; a garbage binner BO wedged CT1 - v3d_gpu.c:774-776). */
static void *block_get(uint32_t pages, int cached, uintptr_t *pa)
{
	uint32_t i;
	void *cpu;
	int flags;

	for (i = 0u; i < srv.npool; i++) {
		if ((srv.pool[i].pages == pages) && (srv.pool[i].cached == (uint32_t)cached)) {
			cpu = srv.pool[i].cpu;
			*pa = srv.pool[i].pa;
			srv.pool[i] = srv.pool[--srv.npool];
			memset(cpu, 0, (size_t)pages * _PAGE_SIZE);
			return cpu;
		}
	}

	flags = MAP_CONTIGUOUS | MAP_ANONYMOUS;
	if (cached == 0) {
		flags |= MAP_UNCACHED;
	}
	cpu = mmap(NULL, (size_t)pages * _PAGE_SIZE, PROT_READ | PROT_WRITE, flags, -1, 0);
	if (cpu == MAP_FAILED) {
		return NULL;
	}
	memset(cpu, 0, (size_t)pages * _PAGE_SIZE);
	*pa = (uintptr_t)va2pa(cpu);
	return cpu;
}


static void block_put(void *cpu, uintptr_t pa, uint32_t pages, int cached)
{
	if (srv.npool < V3DA_MAX_POOL) {
		srv.pool[srv.npool].cpu = cpu;
		srv.pool[srv.npool].pa = pa;
		srv.pool[srv.npool].pages = pages;
		srv.pool[srv.npool].cached = (uint32_t)cached;
		srv.npool++;
		return;
	}
	/* Pool full: the only path that returns pages to the kernel. Counted, and
	 * loud once - the design expects pages_to_kernel == 0 (section 9.2). */
	if (srv.pages_to_kernel == 0u) {
		printf("V3DA srv pool full (%u blocks): returning a block to the kernel\n", V3DA_MAX_POOL);
	}
	srv.pages_to_kernel += pages;
	(void)munmap(cpu, (size_t)pages * _PAGE_SIZE);
}


/* Which firmware-fb buffer a new scanout BO takes: the lowest free one, as the
 * winsys hands buffer 0, 1, 2 to the glue's scanout FBOs in creation order.
 * -1 = none (no SCANOUT_INFO yet, or every buffer taken: a normal BO then, as
 * the winsys does). */
static int scanout_pick(void)
{
	uint32_t i;

	for (i = 0u; i < srv.scan.nbuf; i++) {
		if (srv.scan.claimed[i] == 0u) {
			return (int)i;
		}
	}
	return -1;
}


int v3da_bo_create(uint32_t client, uint32_t size, uint32_t flags, v3da_bo_create_resp_t *out)
{
	uint32_t pages, slot, gpuva, i, scan_pages = 0u, buf_pa = 0u;
	int cached = ((flags & V3DA_BO_CACHEABLE) != 0u) ? 1 : 0;
	int scan = -1;
	uintptr_t pa;
	void *cpu;
	v3da_bo_t *b;

	if ((flags & V3DA_BO_SCANOUT) != 0u) {
		scan = scanout_pick();
		if (scan >= 0) {
			cached = 0;   /* the RT is GPU-written; its own DRAM view stays uncached */
			buf_pa = srv.scan.pa + (uint32_t)scan * srv.scan.bytes;
		}
	}
	if (size > 0x40000000u) {
		return -EINVAL;
	}
	pages = (uint32_t)((size + _PAGE_SIZE - 1u) / _PAGE_SIZE);
	if (pages == 0u) {
		pages = 1u;   /* a zero-byte BO still gets a valid handle and VA (old lane) */
	}

	for (slot = 0u; slot < srv.nbos; slot++) {
		if (srv.bos[slot].state == V3DA_BO_FREE) {
			break;
		}
	}
	if (slot == srv.nbos) {
		if (srv.nbos >= V3DA_MAX_BOS) {
			return -ENOMEM;
		}
		srv.nbos++;
	}

	cpu = block_get(pages, cached, &pa);
	if (cpu == NULL) {
		return -ENOMEM;
	}
	gpuva = v3da_hw_va_alloc(&srv.hw, pages);
	if (gpuva == 0u) {
		block_put(cpu, pa, pages, cached);
		return -ENOMEM;
	}
	/* Map each page by its own physical address (per-page va2pa is correct for
	 * contiguous and non-contiguous backing alike - v3d_gpu.c:778-783). A scanout
	 * BO maps its visible rows to the fb buffer instead; the tile-padding rows
	 * beyond it (1088 stored rows for a 1080 RT) stay on the BO's own DRAM
	 * (winsys ioc_create_bo scanout branch). */
	if (scan >= 0) {
		scan_pages = srv.scan.bytes / (uint32_t)_PAGE_SIZE;
		if (scan_pages > pages) {
			scan_pages = pages;
		}
	}
	for (i = 0u; i < pages; i++) {
		uintptr_t ppa = (i < scan_pages) ? ((uintptr_t)buf_pa + (uintptr_t)i * _PAGE_SIZE) :
			(uintptr_t)va2pa((char *)cpu + (size_t)i * _PAGE_SIZE);
		srv.hw.pt[(gpuva >> V3D_PAGE_SHIFT) + i] = (uint32_t)(ppa >> V3D_PAGE_SHIFT) | PTE_W | PTE_V;
	}
	srv.hw.pt_gen++;
	/* The TLB flush that makes these PTEs visible happens in every job prologue
	 * (E2 step 5, kept); no job can use this BO before its first submit. */

	b = &srv.bos[slot];
	memset(b, 0, sizeof(*b));
	b->state = V3DA_BO_LIVE;
	srv.bo_gen[slot]++;
	if ((srv.bo_gen[slot] & (0xffffffffu >> V3DA_HANDLE_SLOT_BITS)) == 0u) {
		srv.bo_gen[slot] = 1u;   /* keep handles nonzero-generation after a wrap */
	}
	b->handle = (srv.bo_gen[slot] << V3DA_HANDLE_SLOT_BITS) | (slot + 1u);
	b->owner = client;
	b->refs = 1u;
	b->flags = flags;
	b->cpu = cpu;
	b->pa = pa;
	b->gpuva = gpuva;
	b->pages = pages;
	if (cached != 0) {
		b->flags |= V3DA_BO_CACHEABLE;
	}
	else {
		b->flags &= ~V3DA_BO_CACHEABLE;
	}
	if (scan >= 0) {
		b->scanout = scan + 1;
		srv.scan.claimed[scan] = b->handle;
		printf("V3DA srv scanout BO handle=0x%x buf%d pa=0x%08x gpuva=0x%08x %u/%u pages on the fb\n",
			b->handle, scan, buf_pa, gpuva, scan_pages, pages);
	}

	out->handle = b->handle;
	out->gpuva = gpuva;
	out->size = pages * (uint32_t)_PAGE_SIZE;
	out->scanout = (uint32_t)b->scanout;
	out->mem.kind = V3DA_MEM_PHYS;
	out->mem.cache = (cached != 0) ? V3DA_CACHE_CACHED : V3DA_CACHE_UNCACHED;
	out->mem.port = 0u;
	out->mem.size = (uint64_t)pages * _PAGE_SIZE;
	out->mem.addr = (uint64_t)pa;
	return 0;
}


/* Step 1 of the quarantine: unmap from the GPU, flush if nothing runs, record the
 * fence pass. The handle is invalid from here on. */
static void quarantine_begin(v3da_bo_t *b)
{
	uint32_t i;
	int q, busy = 0;

	for (i = 0u; i < b->pages; i++) {
		srv.hw.pt[(b->gpuva >> V3D_PAGE_SHIFT) + i] = 0u;
	}
	srv.hw.pt_gen++;
	if ((b->scanout > 0) && ((uint32_t)b->scanout <= V3DA_SCANOUT_MAX) &&
			(srv.scan.claimed[b->scanout - 1] == b->handle)) {
		srv.scan.claimed[b->scanout - 1] = 0u;   /* the next scanout RT may take it */
	}
	b->clear_pt_gen = srv.hw.pt_gen;
	for (q = 0; q < V3DA_Q_COUNT; q++) {
		b->pass[q] = v3da_load64(&srv.fp->hdr.hw_submitted[q]);
		if ((q != V3DA_Q_CPU) && (srv.q[q].active != NULL)) {
			busy = 1;
		}
	}
	if (busy == 0) {
		v3da_hw_mmu_flush(&srv.hw);   /* else: the next job prologue flushes (step 5) */
	}
	b->state = V3DA_BO_QUARANTINE;
}


/* Validate every handle, then pin them all (a submit names each BO once or more;
 * duplicates are pinned once per mention and unpinned the same way). */
int v3da_bo_pin_for_job(const uint32_t *handles, uint32_t n)
{
	uint32_t i;

	for (i = 0u; i < n; i++) {
		if (v3da_bo_find(handles[i]) == NULL) {
			return -ENOENT;
		}
	}
	for (i = 0u; i < n; i++) {
		v3da_bo_find(handles[i])->inflight++;
	}
	return 0;
}


/* Implicit sync: the BO's last user on this queue is `f` (WAIT_BO waits for all). */
void v3da_bo_mark_use(const uint32_t *handles, uint32_t n, const v3da_fence_t *f)
{
	uint32_t i;
	v3da_bo_t *b;

	for (i = 0u; i < n; i++) {
		b = bo_lookup(handles[i]);
		if ((b != NULL) && (f->queue < V3DA_Q_COUNT)) {
			b->last[f->queue] = *f;
		}
	}
}


void v3da_bo_unpin(const uint32_t *handles, uint32_t n)
{
	uint32_t i;
	v3da_bo_t *b;

	for (i = 0u; i < n; i++) {
		b = bo_lookup(handles[i]);
		if (b == NULL) {
			continue;
		}
		if (b->inflight > 0u) {
			b->inflight--;
		}
		if ((b->refs == 0u) && (b->inflight == 0u)) {
			quarantine_begin(b);
		}
	}
}


/* The binner-overflow pool (old daemon, v3d_gpu.c:644-665): uncached contiguous
 * pages, zeroed, premapped at one stable GPU VA so a grant never needs a TLB
 * flush. Owned by the server for its whole life. */
int v3da_bo_map_ovf_pool(uint32_t bytes, uint32_t *gpuva)
{
	uint32_t pages = bytes / (uint32_t)_PAGE_SIZE, va, i;
	void *cpu;

	va = v3da_hw_va_alloc(&srv.hw, pages);
	if (va == 0u) {
		return -ENOMEM;
	}
	cpu = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_UNCACHED | MAP_CONTIGUOUS | MAP_ANONYMOUS, -1, 0);
	if (cpu == MAP_FAILED) {
		v3da_hw_va_free(&srv.hw, va, pages);
		return -ENOMEM;
	}
	memset(cpu, 0, bytes);
	for (i = 0u; i < pages; i++) {
		uintptr_t ppa = (uintptr_t)va2pa((char *)cpu + (size_t)i * _PAGE_SIZE);
		srv.hw.pt[(va >> V3D_PAGE_SHIFT) + i] = (uint32_t)(ppa >> V3D_PAGE_SHIFT) | PTE_W | PTE_V;
	}
	srv.hw.pt_gen++;
	v3da_hw_mmu_flush(&srv.hw);
	*gpuva = va;
	return 0;
}


int v3da_bo_close(uint32_t client, uint32_t handle)
{
	v3da_bo_t *b = v3da_bo_find(handle);

	(void)client;
	if (b == NULL) {
		return 0;   /* already gone / never ours: 0, as the winsys and old daemon */
	}
	if (b->refs > 0u) {
		b->refs--;
	}
	if ((b->refs == 0u) && (b->inflight == 0u)) {
		quarantine_begin(b);
		v3da_bo_quarantine_poll();
	}
	return 0;
}


/* Steps 2-4: a quarantined BO whose unmap is flushed out of the TLB and which
 * every queue has passed goes back to the pool; its VA range back to the holes. */
void v3da_bo_quarantine_poll(void)
{
	uint32_t i;
	int q, ok;
	v3da_bo_t *b;

	for (i = 0u; i < srv.nbos; i++) {
		b = &srv.bos[i];
		if (b->state != V3DA_BO_QUARANTINE) {
			continue;
		}
		if (srv.hw.tlb_gen < b->clear_pt_gen) {
			continue;
		}
		ok = 1;
		for (q = 0; q < V3DA_Q_COUNT; q++) {
			if (v3da_load64(&srv.fp->hdr.hw_completed[q]) < b->pass[q]) {
				ok = 0;
				break;
			}
		}
		if (ok == 0) {
			continue;
		}
		block_put(b->cpu, b->pa, b->pages, ((b->flags & V3DA_BO_CACHEABLE) != 0u) ? 1 : 0);
		v3da_hw_va_free(&srv.hw, b->gpuva, b->pages);
		memset(b, 0, sizeof(*b));
		b->state = V3DA_BO_FREE;
		srv.bo_quarantine_passed++;
	}
}


int v3da_bo_mmap(uint32_t handle, v3da_bo_resp_t *out)
{
	v3da_bo_t *b = v3da_bo_find(handle);

	if (b == NULL) {
		return -EINVAL;
	}
	out->gpuva = b->gpuva;
	out->size = b->pages * (uint32_t)_PAGE_SIZE;
	out->mem.kind = V3DA_MEM_PHYS;
	out->mem.cache = ((b->flags & V3DA_BO_CACHEABLE) != 0u) ? V3DA_CACHE_CACHED : V3DA_CACHE_UNCACHED;
	out->mem.port = 0u;
	out->mem.size = (uint64_t)b->pages * _PAGE_SIZE;
	out->mem.addr = (uint64_t)b->pa;
	return 0;
}


int v3da_bo_offset(uint32_t handle, uint32_t *gpuva)
{
	v3da_bo_t *b = v3da_bo_find(handle);

	if (b == NULL) {
		return -EINVAL;
	}
	*gpuva = b->gpuva;
	return 0;
}


/* FNV-1a over the SERVER's view of a BO range: the client compares it with its
 * own, proving both mappings reach the same bytes with the same memory type. */
int v3da_bo_checksum(uint32_t handle, uint32_t off, uint32_t len, v3da_bo_checksum_resp_t *out)
{
	v3da_bo_t *b = v3da_bo_find(handle);
	const volatile uint8_t *p;
	uint32_t h = 2166136261u, i, size;

	if (b == NULL) {
		return -EINVAL;
	}
	size = b->pages * (uint32_t)_PAGE_SIZE;
	if ((off > size) || (len > size - off) || ((off & 3u) != 0u)) {
		return -EINVAL;
	}
	p = (const volatile uint8_t *)b->cpu + off;
	for (i = 0u; i < len; i++) {
		h ^= p[i];
		h *= 16777619u;
	}
	out->sum = h;
	out->first_word = (len >= 4u) ? *(const volatile uint32_t *)p : 0u;
	return 0;
}


/* A client is gone: drop the creator reference of each BO it made. BOs still in
 * flight or imported by others survive until their last reference. */
void v3da_bo_client_gone(uint32_t client)
{
	uint32_t i;
	v3da_bo_t *b;

	for (i = 0u; i < srv.nbos; i++) {
		b = &srv.bos[i];
		if ((b->state != V3DA_BO_LIVE) || (b->owner != client) || (b->refs == 0u)) {
			continue;
		}
		b->owner = 0u;
		if (b->refs > 0u) {
			b->refs--;
		}
		if ((b->refs == 0u) && (b->inflight == 0u)) {
			quarantine_begin(b);
		}
	}
	v3da_bo_quarantine_poll();
}


void v3da_bo_counts(uint32_t *live, uint32_t *quar, uint32_t *pooled)
{
	uint32_t i;

	*live = 0u;
	*quar = 0u;
	for (i = 0u; i < srv.nbos; i++) {
		if (srv.bos[i].state == V3DA_BO_LIVE) {
			(*live)++;
		}
		else if (srv.bos[i].state == V3DA_BO_QUARANTINE) {
			(*quar)++;
		}
	}
	*pooled = srv.npool;
}
