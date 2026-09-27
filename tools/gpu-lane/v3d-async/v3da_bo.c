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
 *   - PRIME export (G4): a BO's block is published with memExport() under
 *     {srv.buf_port, handle} and served as V3DA_BUF_NS "/<handle>"; importing clients
 *     share THE BO (its handle, GPU VA and last-use record) and each holds a
 *     reference, as does every open descriptor of the name. Pool reuse of a block
 *     whose export window some process still maps (after every handle and descriptor
 *     is gone) is the same trade as a stale MAP_PHYSMEM mapping: the late access lands
 *     in another GPU buffer, never in memory the kernel recycles.
 *   - Blocks go to a server-owned POOL, not back to the kernel: a stale device or
 *     stale client MAP_PHYSMEM write then lands in another GPU buffer, never in a
 *     malloc heap (the C1 class), and the last-munmap-of-a-contiguous-object kernel
 *     bug (E1 section 6) is not exercised.
 *
 * All functions run with srv.lock held, except v3da_bo_import (takes it itself) and
 * the namespace thread v3da_bufns_thread.
 *
 * Copyright 2026 Phoenix Systems
 * Author: Witold Bołt
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/file.h>
#include <sys/mman.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <sys/threads.h>

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


static void export_withdraw(v3da_bo_t *b);


/* Step 1 of the quarantine: unmap from the GPU, flush if nothing runs, record the
 * fence pass. The handle is invalid from here on. */
static void quarantine_begin(v3da_bo_t *b)
{
	uint32_t i;
	int q, busy = 0;

	export_withdraw(b);   /* normally gone already: bo_unref withdraws at the last reference */

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


static uint64_t client_bit(uint32_t client)
{
	return ((client >= 1u) && (client <= V3DA_MAX_CLIENTS)) ? (1ULL << (client - 1u)) : 0u;
}


/* The /v3dbuf name goes the moment nothing references the BO any more. G3 ordering
 * (as kms_bo_unref): the flag the namespace thread answers atSize from is cleared
 * BEFORE memUnexport, both under srv.lock, so once the kernel's object tree has lost
 * the window no atSize for this id is answered positively (no shadow object, E1 3). */
static void export_withdraw(v3da_bo_t *b)
{
	oid_t oid;

	if (b->exported == 0) {
		return;
	}
	b->exported = 0;
	oid.port = srv.buf_port;
	oid.id = b->handle;
	(void)memUnexport(&oid);
	srv.exports--;
	printf("V3DA srv export withdrawn handle=0x%x live=%u\n", b->handle, srv.exports);
}


/* Drop one reference (a handle or a descriptor of the name). At the last one the
 * name is withdrawn; the block is quarantined once no job references it either. */
static void bo_unref(v3da_bo_t *b)
{
	if (b->refs > 0u) {
		b->refs--;
	}
	if (b->refs == 0u) {
		export_withdraw(b);
		if (b->inflight == 0u) {
			quarantine_begin(b);
		}
	}
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
	uint64_t bit = client_bit(client);

	if (b == NULL) {
		return 0;   /* already gone / never ours: 0, as the winsys and old daemon */
	}
	if ((b->sharers & bit) != 0u) {
		b->sharers &= ~bit;   /* this client's import of another client's export */
	}
	else if (b->owner != 0u) {
		b->owner = 0u;        /* the creator's handle (any client may close it: M1 semantics) */
	}
	else {
		return 0;             /* only descriptors of its name hold it: nothing of this client's to drop */
	}
	bo_unref(b);
	v3da_bo_quarantine_poll();
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
		if (b->imported != 0) {
			/* Another server's pages: drop our window reference, never pool them
			 * (block_get zeroes pooled blocks - it would wipe the exporter's buffer). */
			(void)munmap(b->cpu, (size_t)b->pages * _PAGE_SIZE);
			srv.imports--;
			printf("V3DA srv import released handle=0x%x id=%llu pages=%u live=%u\n", b->handle,
				(unsigned long long)b->imp_mem.addr, b->pages, srv.imports);
		}
		else {
			block_put(b->cpu, b->pa, b->pages, ((b->flags & V3DA_BO_CACHEABLE) != 0u) ? 1 : 0);
		}
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
	if (b->imported != 0) {
		out->mem = b->imp_mem;   /* the exporter's name: map /kmsbuf/<id>, never its physical address */
		return 0;
	}
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


/* A client is gone: drop its references - the creator reference of each BO it made
 * and each import it held. BOs still in flight, imported by others or open as a
 * /v3dbuf descriptor survive until their last reference. */
void v3da_bo_client_gone(uint32_t client)
{
	uint64_t bit = client_bit(client);
	uint32_t i;
	v3da_bo_t *b;

	for (i = 0u; i < srv.nbos; i++) {
		b = &srv.bos[i];
		if ((b->state != V3DA_BO_LIVE) || (b->refs == 0u)) {
			continue;
		}
		if (b->owner == client) {
			b->owner = 0u;
			bo_unref(b);
		}
		if ((b->state == V3DA_BO_LIVE) && ((b->sharers & bit) != 0u)) {
			b->sharers &= ~bit;
			bo_unref(b);
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


/* ========================================================================= */
/* PRIME import (M3 part 2, gap G1)                                           */
/* ========================================================================= */

/* This client's live import of {port, id}, if any (DRM: a second import of the
 * same dma-buf on one file returns the same handle, with no extra reference). */
static v3da_bo_t *import_find(uint32_t client, uint32_t port, uint64_t id)
{
	uint32_t i;
	v3da_bo_t *b;

	for (i = 0u; i < srv.nbos; i++) {
		b = &srv.bos[i];
		if ((b->state == V3DA_BO_LIVE) && (b->imported != 0) && (b->refs > 0u) && (b->owner == client) &&
				(b->imp_mem.port == port) && (b->imp_mem.addr == id)) {
			return b;
		}
	}
	return NULL;
}


static void import_reply(const v3da_bo_t *b, v3da_bo_create_resp_t *out)
{
	memset(out, 0, sizeof(*out));
	out->handle = b->handle;
	out->gpuva = b->gpuva;
	out->size = b->pages * (uint32_t)_PAGE_SIZE;
	out->scanout = 0u;
	out->mem = b->imp_mem;
}


/* Open the buffer name, size it, map it with the export's memory type and resolve
 * every page. Unlocked: open() and lseek() are IPC to the exporter. */
static int import_map(const v3da_bo_import_req_t *rq, void **cpu_out, uintptr_t **pa_out, uint32_t *pages_out,
	int *contig_out)
{
	char path[48];
	oid_t dev;
	off_t end;
	uint64_t size = rq->size;
	uint32_t pages, i;
	uintptr_t *pa;
	void *cpu;
	int fd, flags, e;

	(void)snprintf(path, sizeof(path), "%s/%llu", V3DA_IMPORT_KMSBUF_DIR, (unsigned long long)rq->id);
	if (lookup(path, NULL, &dev) < 0) {
		return -ENOENT;   /* not (or no longer) exported */
	}
	if ((dev.port != rq->port) || ((uint64_t)dev.id != rq->id)) {
		return -EINVAL;   /* the name is served by another port than the client resolved */
	}
	fd = open(path, O_RDONLY);   /* O_RDONLY: O_RDWR would stat() the name (E1 section 1) */
	if (fd < 0) {
		return (errno != 0) ? -errno : -EIO;
	}
	end = lseek(fd, 0, SEEK_END);   /* G3: the exporter answers atSize while the buffer is exported */
	if (size == 0u) {
		if (end <= 0) {
			(void)close(fd);
			return -EINVAL;   /* size unknown: the exporter predates G3 and the client did not know it */
		}
		size = (uint64_t)end;
	}
	else if ((end > 0) && (size > (uint64_t)end)) {
		(void)close(fd);
		return -EINVAL;
	}
	if (((size & (_PAGE_SIZE - 1u)) != 0u) || (size > V3DA_IMPORT_MAX_SIZE)) {
		(void)close(fd);
		return -EINVAL;
	}
	flags = MAP_SHARED | ((rq->cache == V3DA_CACHE_UNCACHED) ? MAP_UNCACHED : 0);
	cpu = mmap(NULL, (size_t)size, PROT_READ, flags, fd, 0);
	e = errno;
	(void)close(fd);   /* the mapping holds the export window (E1) */
	if (cpu == MAP_FAILED) {
		return (e != 0) ? -e : -EINVAL;
	}
	pages = (uint32_t)(size / _PAGE_SIZE);
	pa = malloc((size_t)pages * sizeof(*pa));
	if (pa == NULL) {
		(void)munmap(cpu, (size_t)size);
		return -ENOMEM;
	}
	*contig_out = 1;
	for (i = 0u; i < pages; i++) {
		volatile const uint32_t *p = (volatile const uint32_t *)((uintptr_t)cpu + (size_t)i * _PAGE_SIZE);
		(void)*p;   /* fault the page in: va2pa reports present pages only */
		pa[i] = (uintptr_t)va2pa((void *)(uintptr_t)p);
		if ((pa[i] == (uintptr_t)(addr_t)-1) || ((pa[i] & (_PAGE_SIZE - 1u)) != 0u) ||
				((uint64_t)pa[i] >= (1ULL << (32u + V3D_PAGE_SHIFT)))) {
			free(pa);
			(void)munmap(cpu, (size_t)size);
			return -EFAULT;
		}
		if (pa[i] != pa[0] + (uintptr_t)i * _PAGE_SIZE) {
			*contig_out = 0;
		}
	}
	*cpu_out = cpu;
	*pa_out = pa;
	*pages_out = pages;
	return 0;
}


static void export_memref(const v3da_bo_t *b, v3da_memref_t *m)
{
	memset(m, 0, sizeof(*m));
	m->kind = V3DA_MEM_OID;
	m->cache = V3DA_CACHE_UNCACHED;   /* only uncached BOs are exported (v3da_bo_export) */
	m->port = srv.buf_port;
	m->size = (uint64_t)b->pages * _PAGE_SIZE;
	m->addr = b->handle;
}


/* BO_IMPORT ns=v3dbuf: the importer shares the exported BO itself (one GPU VA, one
 * last-use record for BO_WAIT) and holds a reference on it; importing one of its own
 * exports, or importing again, returns the same handle with no extra reference
 * (DRM). Entirely under srv.lock: nothing to open, the server is the exporter. */
static int import_v3dbuf(uint32_t client, const v3da_bo_import_req_t *rq, v3da_bo_create_resp_t *out)
{
	uint64_t bit = client_bit(client);
	v3da_bo_t *b = NULL;
	int rc, self = 0;

	(void)mutexLock(srv.lock);
	if (srv.clients[client - 1u].used == 0) {
		rc = -EBADF;
	}
	else if ((srv.buf_port == 0u) || (rq->port != srv.buf_port)) {
		rc = -EINVAL;   /* not this server's namespace */
	}
	else if ((rq->id > 0xffffffffu) || ((b = v3da_bo_find((uint32_t)rq->id)) == NULL) || (b->exported == 0)) {
		rc = -ENOENT;   /* not (or no longer) exported */
	}
	else if (((rq->size & (_PAGE_SIZE - 1u)) != 0u) || (rq->size > (uint64_t)b->pages * _PAGE_SIZE)) {
		rc = -EINVAL;
	}
	else {
		self = ((b->owner == client) || ((b->sharers & bit) != 0u)) ? 1 : 0;
		if (self == 0) {
			b->sharers |= bit;
			b->refs++;
		}
		memset(out, 0, sizeof(*out));
		out->handle = b->handle;
		out->gpuva = b->gpuva;
		out->size = b->pages * (uint32_t)_PAGE_SIZE;
		out->scanout = 0u;
		export_memref(b, &out->mem);
		printf("V3DA srv import handle=0x%x client=%u ns=v3dbuf id=%llu pages=%u owner=%u self=%d refs=%u opens=%u\n",
			b->handle, client, (unsigned long long)rq->id, b->pages, b->owner, self, b->refs, b->fd_opens);
		rc = 0;
	}
	(void)mutexUnlock(srv.lock);
	if (rc != 0) {
		printf("V3DA srv import FAIL client=%u ns=v3dbuf id=%llu port=%u size=%llu rc=%d\n", client,
			(unsigned long long)rq->id, rq->port, (unsigned long long)rq->size, rc);
	}
	return rc;
}


int v3da_bo_import(uint32_t client, const v3da_bo_import_req_t *rq, v3da_bo_create_resp_t *out)
{
	uint32_t pages = 0u, slot, gpuva, i;
	uint64_t gen;
	uintptr_t *pa = NULL;
	void *cpu = NULL;
	int rc, contig = 0;
	v3da_bo_t *b;

	if ((rq->pad != 0u) || (client < 1u) || (client > V3DA_MAX_CLIENTS)) {
		return -EINVAL;
	}
	if (rq->ns == V3DA_IMPORT_NS_V3DBUF) {
		return import_v3dbuf(client, rq, out);   /* one of this server's own exports (G4) */
	}
	if ((rq->ns != V3DA_IMPORT_NS_KMSBUF) || (rq->cache > V3DA_CACHE_UNCACHED)) {
		return -EINVAL;
	}

	(void)mutexLock(srv.lock);
	if (srv.clients[client - 1u].used == 0) {
		(void)mutexUnlock(srv.lock);
		return -EBADF;
	}
	gen = srv.slot_gen[client - 1u];
	b = import_find(client, rq->port, rq->id);
	if (b != NULL) {
		import_reply(b, out);
		(void)mutexUnlock(srv.lock);
		return 0;
	}
	(void)mutexUnlock(srv.lock);

	rc = import_map(rq, &cpu, &pa, &pages, &contig);
	if (rc != 0) {
		printf("V3DA srv import FAIL client=%u id=%llu port=%u size=%llu rc=%d\n", client, (unsigned long long)rq->id,
			rq->port, (unsigned long long)rq->size, rc);
		return rc;
	}

	(void)mutexLock(srv.lock);
	if ((srv.clients[client - 1u].used == 0) || (srv.slot_gen[client - 1u] != gen)) {
		rc = -EBADF;   /* the client closed while we mapped */
	}
	else if ((b = import_find(client, rq->port, rq->id)) != NULL) {
		import_reply(b, out);   /* a racing import of the same buffer won */
		rc = 1;
	}
	else {
		for (slot = 0u; slot < srv.nbos; slot++) {
			if (srv.bos[slot].state == V3DA_BO_FREE) {
				break;
			}
		}
		if ((slot == srv.nbos) && (srv.nbos >= V3DA_MAX_BOS)) {
			rc = -ENOMEM;
		}
		else if ((gpuva = v3da_hw_va_alloc(&srv.hw, pages)) == 0u) {
			rc = -ENOMEM;
		}
		else {
			if (slot == srv.nbos) {
				srv.nbos++;
			}
			for (i = 0u; i < pages; i++) {
				srv.hw.pt[(gpuva >> V3D_PAGE_SHIFT) + i] = (uint32_t)(pa[i] >> V3D_PAGE_SHIFT) | PTE_W | PTE_V;
			}
			srv.hw.pt_gen++;   /* flushed by the next job prologue (E2 step 5), as bo_create */

			b = &srv.bos[slot];
			memset(b, 0, sizeof(*b));
			b->state = V3DA_BO_LIVE;
			srv.bo_gen[slot]++;
			if ((srv.bo_gen[slot] & (0xffffffffu >> V3DA_HANDLE_SLOT_BITS)) == 0u) {
				srv.bo_gen[slot] = 1u;
			}
			b->handle = (srv.bo_gen[slot] << V3DA_HANDLE_SLOT_BITS) | (slot + 1u);
			b->owner = client;
			b->refs = 1u;
			b->flags = (rq->cache == V3DA_CACHE_CACHED) ? V3DA_BO_CACHEABLE : 0u;
			b->cpu = cpu;
			b->pa = pa[0];
			b->gpuva = gpuva;
			b->pages = pages;
			b->imported = 1;
			b->imp_mem.kind = V3DA_MEM_OID;
			b->imp_mem.cache = (uint16_t)rq->cache;
			b->imp_mem.port = rq->port;
			b->imp_mem.size = (uint64_t)pages * _PAGE_SIZE;
			b->imp_mem.addr = rq->id;
			srv.imports++;
			import_reply(b, out);
			printf("V3DA srv import handle=0x%x client=%u ns=kmsbuf id=%llu pages=%u pa0=0x%08llx contiguous=%d "
				"gpuva=0x%08x cache=%s live=%u\n", b->handle, client, (unsigned long long)rq->id, pages,
				(unsigned long long)pa[0], contig, gpuva, (rq->cache == V3DA_CACHE_CACHED) ? "cached" : "uncached",
				srv.imports);
			rc = 0;
		}
	}
	(void)mutexUnlock(srv.lock);

	free(pa);
	if (rc != 0) {
		(void)munmap(cpu, (size_t)pages * _PAGE_SIZE);
	}
	return (rc == 1) ? 0 : rc;
}


/* ========================================================================= */
/* PRIME export (gap G4) and the /v3dbuf namespace                            */
/* ========================================================================= */

int v3da_bo_export(uint32_t client, uint32_t handle, v3da_bo_resp_t *out)
{
	v3da_bo_t *b = v3da_bo_find(handle);
	oid_t oid;
	int rc;

	if ((b == NULL) || ((b->owner != client) && ((b->sharers & client_bit(client)) == 0u))) {
		return -ENOENT;   /* DRM: not a handle of this file */
	}
	/* An import's pages are another server's window (E1 refuses windows of windows:
	 * the client reopens the exporter's name, G4a); a scanout BO's CPU block is not
	 * what the GPU writes; a cacheable export would need its memory type carried to
	 * every importer, which a dma-buf descriptor cannot (libdrm-phoenix creates
	 * uncached BOs only). */
	if ((b->imported != 0) || (b->scanout > 0) || ((b->flags & V3DA_BO_CACHEABLE) != 0u)) {
		return -EINVAL;
	}
	if (srv.buf_port == 0u) {
		return -ENODEV;
	}
	if (b->exported == 0) {
		oid.port = srv.buf_port;
		oid.id = b->handle;
		rc = memExport(&oid, b->cpu, (size_t)b->pages * _PAGE_SIZE);
		if (rc != 0) {
			printf("V3DA srv export FAIL handle=0x%x client=%u pages=%u rc=%d\n", handle, client, b->pages, rc);
			return (rc < 0) ? rc : -EIO;
		}
		b->exported = 1;
		srv.exports++;
		printf("V3DA srv export handle=0x%x client=%u ns=v3dbuf id=%u pages=%u pa=0x%08llx gpuva=0x%08x live=%u\n",
			b->handle, client, b->handle, b->pages, (unsigned long long)b->pa, b->gpuva, srv.exports);
	}
	out->gpuva = b->gpuva;
	out->size = b->pages * (uint32_t)_PAGE_SIZE;
	export_memref(b, &out->mem);
	return 0;
}


/* A published, referenced BO by its export id (locked). */
static v3da_bo_t *bo_by_export(uint64_t id)
{
	v3da_bo_t *b = ((id != 0u) && (id <= 0xffffffffu)) ? bo_lookup((uint32_t)id) : NULL;

	return ((b != NULL) && (b->exported != 0) && (b->refs > 0u)) ? b : NULL;
}


/* V3DA_BUF_NS on its own port (the main port's mtOpen hands out client ids, which
 * would rewrite a buffer descriptor's oid). E1 section 1, as rpi4-kms's /kmsbuf:
 * mtLookup "<id>", atMode, mtOpen/mtClose replying 0, atSize only while exported.
 * Each open descriptor of an exported BO holds one reference on it (fd_opens). */
void v3da_bufns_thread(void *arg)
{
	static uint32_t notes;
	msg_t msg;
	msg_rid_t rid;
	char name[24], *end;
	unsigned long long id;
	size_t len;
	v3da_bo_t *b;
	int err;

	(void)arg;
	for (;;) {
		err = msgRecv(srv.buf_port, &msg, &rid);
		if (err < 0) {
			if (err == -EINTR) {
				continue;
			}
			break;   /* port gone (server exiting) */
		}
		switch (msg.type) {
			case mtLookup:
				len = (msg.i.data != NULL) ? strnlen(msg.i.data, msg.i.size) : 0u;
				if ((len == 0u) || (len >= sizeof(name))) {
					msg.o.err = -ENOENT;
					break;
				}
				memcpy(name, msg.i.data, len);
				name[len] = '\0';
				id = strtoull(name, &end, 10);
				(void)mutexLock(srv.lock);
				b = ((*end == '\0') && (name[0] >= '1') && (name[0] <= '9')) ? bo_by_export(id) : NULL;
				(void)mutexUnlock(srv.lock);
				if (b == NULL) {
					msg.o.err = -ENOENT;
					break;
				}
				msg.o.lookup.fil.port = srv.buf_port;
				msg.o.lookup.fil.id = (id_t)id;
				msg.o.lookup.dev = msg.o.lookup.fil;
				msg.o.err = (int)len;
				break;

			case mtGetAttr:
				if (msg.i.attr.type == atMode) {
					msg.o.attr.val = (msg.oid.id == 0u) ? (S_IFDIR | 0555) : (S_IFCHR | 0666);
					msg.o.err = 0;
				}
				else if (msg.i.attr.type == atType) {
					msg.o.attr.val = (msg.oid.id == 0u) ? otDir : otDev;
					msg.o.err = 0;
				}
				else if ((msg.i.attr.type == atSize) && (msg.oid.id != 0u)) {
					/* lseek(dmabuf, 0, SEEK_END) (G3), and what the kernel asks when mmap()
					 * misses the object tree - answered ONLY while exported, under the
					 * lock export_withdraw holds (no shadow object, E1 section 3) */
					(void)mutexLock(srv.lock);
					b = bo_by_export(msg.oid.id);
					msg.o.attr.val = (b != NULL) ? (long long)b->pages * _PAGE_SIZE : 0;
					msg.o.err = (b != NULL) ? 0 : -ENOENT;
					(void)mutexUnlock(srv.lock);
				}
				else {
					msg.o.err = -ENOENT;
				}
				break;

			case mtGetAttrAll:
				(void)mutexLock(srv.lock);
				b = (msg.oid.id != 0u) ? bo_by_export(msg.oid.id) : NULL;
				len = (b != NULL) ? (size_t)b->pages * _PAGE_SIZE : 0u;
				(void)mutexUnlock(srv.lock);
				if ((msg.oid.id != 0u) && (b == NULL)) {
					msg.o.err = -ENOENT;
				}
				else {
					msg.o.err = v3da_attr_all(&msg, (msg.oid.id == 0u) ? (S_IFDIR | 0555) : (S_IFCHR | 0666),
						(uint64_t)len, srv.buf_port);
				}
				break;

			case mtOpen:
				/* 0, never an id: a positive reply would rewrite the descriptor's oid */
				(void)mutexLock(srv.lock);
				if (msg.oid.id == 0u) {
					msg.o.err = 0;
				}
				else if ((b = bo_by_export(msg.oid.id)) == NULL) {
					msg.o.err = -ENOENT;
				}
				else {
					b->fd_opens++;
					b->refs++;
					msg.o.err = 0;
					if ((srv.verbose != 0) || (notes < 16u)) {
						notes++;
						printf("V3DA srv v3dbuf open id=%u pid=%d opens=%u refs=%u\n", b->handle, msg.pid, b->fd_opens,
							b->refs);
					}
				}
				(void)mutexUnlock(srv.lock);
				break;

			case mtClose:
				(void)mutexLock(srv.lock);
				b = ((msg.oid.id != 0u) && (msg.oid.id <= 0xffffffffu)) ? bo_lookup((uint32_t)msg.oid.id) : NULL;
				if ((b != NULL) && (b->fd_opens > 0u)) {
					b->fd_opens--;
					if ((srv.verbose != 0) || (notes < 16u)) {
						notes++;
						printf("V3DA srv v3dbuf close id=%u pid=%d opens=%u refs=%u\n", b->handle, msg.pid, b->fd_opens,
							b->refs - 1u);
					}
					bo_unref(b);
					v3da_bo_quarantine_poll();
				}
				(void)mutexUnlock(srv.lock);
				msg.o.err = 0;
				break;

			default:
				msg.o.err = -ENOSYS;
				break;
		}
		(void)msgRespond(srv.buf_port, &msg, rid);
	}
	endthread();
}
