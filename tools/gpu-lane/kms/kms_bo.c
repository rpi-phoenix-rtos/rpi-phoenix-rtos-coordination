/*
 * Phoenix-RTOS
 *
 * Raspberry Pi 4 (BCM2711) display server rpi4-kms - dumb BOs, framebuffers,
 * property blobs and the /kmsbuf buffer namespace
 *
 * Scan-out memory is ONE physically contiguous pool reserved at start
 * (MAP_CONTIGUOUS | MAP_UNCACHED; the buddy allocator gives no placement, so the
 * pool is retried until it ends below 1 GiB - the firmware scans nothing above,
 * E3/E6 `range hi`) and sub-allocated
 * exact-size, page-granular. Every pool BO is published with memExport() under
 * {buffer port, handle}; a client maps it with open("/kmsbuf/<handle>") +
 * mmap(MAP_UNCACHED) - the same pages, zero-copy (E1). The "pan" backend's
 * flippable BOs are firmware-framebuffer slots instead: firmware memory is
 * MAP_PHYSMEM, which memExport refuses, so they are handed out by physical
 * address (KMS_MEM_PHYS), as the old lane does today.
 *
 * Lifetime (research section 4.9, the C1 rule): a BO is freed only when its
 * handle is closed AND no framebuffer references it; a framebuffer is released
 * only when it was removed AND no plane shows it or is about to (a flip holds a
 * reference on the incoming framebuffer and drops the outgoing one only when the
 * flip has completed at a vblank).
 *
 * Copyright 2026 Phoenix Systems
 * Author: Witold Bołt
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/file.h>
#include <sys/mman.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <sys/threads.h>

#include "kms.h"


/* ========================================================================= */
/* Pool                                                                       */
/* ========================================================================= */

/* Take up to NTRY pool-sized contiguous blocks until one ends at or below
 * srv.pool_max_end, give the rejects back (safe since the E1 section 6 object-
 * tree fix, kernel d0fb0ca9 / build 8). The pages are not zeroed; every BO is
 * zeroed when handed out. */
int kms_pool_init(void)
{
	enum { NTRY = 16 };
	void *rej[NTRY];
	uint64_t pa, paLast;
	size_t size = (size_t)srv.pool_mib * KMS_MIB;
	int n, i, found = 0;

	if (size == 0u) {
		return -EINVAL;
	}
	for (n = 0; n < NTRY; n++) {
		void *va = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_CONTIGUOUS | MAP_UNCACHED | MAP_ANONYMOUS, -1, 0);
		if (va == MAP_FAILED) {
			break;
		}
		rej[n] = va;
		((volatile uint32_t *)va)[0] = 0u;   /* present before va2pa */
		pa = (uint64_t)va2pa(va);
		paLast = (uint64_t)va2pa((uint8_t *)va + size - _PAGE_SIZE);
		if ((pa == (uint64_t)(addr_t)-1) || (paLast != pa + size - _PAGE_SIZE)) {
			continue;   /* not contiguous as seen by va2pa: never hand it to a device */
		}
		if (pa + size <= srv.pool_max_end) {
			srv.pool_va = va;
			srv.pool_pa = pa;
			srv.pool_size = size;
			rej[n] = NULL;
			found = 1;
			n++;
			break;
		}
	}
	for (i = 0; i < n; i++) {
		if (rej[i] != NULL) {
			(void)munmap(rej[i], size);
		}
	}
	if (!found) {
		KMS_LOG("pool FAIL size_mib=%u tries=%d max_end=0x%llx", srv.pool_mib, n, (unsigned long long)srv.pool_max_end);
		return -ENOMEM;
	}
	srv.pool_ok = 1;
	KMS_LOG("pool pa=0x%llx size_mib=%u tries=%d below_1g=%d below_4g=%d max_end=0x%llx", (unsigned long long)srv.pool_pa,
		srv.pool_mib, n, (srv.pool_pa + size <= KMS_GIB) ? 1 : 0, (srv.pool_pa + size <= 0x100000000ULL) ? 1 : 0,
		(unsigned long long)srv.pool_max_end);
	return 0;
}


void kms_pool_fini(void)
{
	uint32_t i;
	oid_t oid;

	for (i = 0u; i < KMS_MAX_BOS; i++) {
		if (srv.bos[i].used && srv.bos[i].exported) {
			oid.port = srv.buf_port;
			oid.id = srv.bos[i].handle;
			(void)memUnexport(&oid);
			srv.bos[i].exported = 0;
		}
	}
}


/* Lowest pool offset where `size` bytes fit between the live pool BOs. */
static int pool_find(size_t size, size_t *off)
{
	size_t cand[KMS_MAX_BOS + 1];
	uint32_t nc = 0u, i, j;
	size_t best = (size_t)-1;

	cand[nc++] = 0u;
	for (i = 0u; i < KMS_MAX_BOS; i++) {
		if (srv.bos[i].used && (srv.bos[i].kind == KMS_BOK_POOL)) {
			cand[nc++] = srv.bos[i].off + srv.bos[i].size;
		}
	}
	for (j = 0u; j < nc; j++) {
		size_t c = cand[j];
		int ok = (c + size <= srv.pool_size);

		for (i = 0u; ok && (i < KMS_MAX_BOS); i++) {
			const kms_bo_t *b = &srv.bos[i];
			if (b->used && (b->kind == KMS_BOK_POOL) && (c < b->off + b->size) && (b->off < c + size)) {
				ok = 0;
			}
		}
		if (ok && (c < best)) {
			best = c;
		}
	}
	if (best == (size_t)-1) {
		return -ENOMEM;
	}
	*off = best;
	return 0;
}


static void memref_fill(const kms_bo_t *b, kms_memref_t *m)
{
	memset(m, 0, sizeof(*m));
	m->cache = KMS_CACHE_UNCACHED;
	m->size = b->size;
	if (b->kind == KMS_BOK_POOL) {
		m->kind = KMS_MEM_OID;
		m->port = srv.buf_port;
		m->addr = b->handle;
	}
	else {
		m->kind = KMS_MEM_PHYS;
		m->addr = b->pa;
	}
}


/* ========================================================================= */
/* Dumb BOs                                                                   */
/* ========================================================================= */

static kms_bo_t *bo_slot_free(uint32_t *idx)
{
	uint32_t i;

	for (i = 0u; i < KMS_MAX_BOS; i++) {
		if (!srv.bos[i].used) {
			*idx = i;
			return &srv.bos[i];
		}
	}
	return NULL;
}


kms_bo_t *kms_bo_get(uint32_t client, uint32_t handle)
{
	uint32_t i;

	for (i = 0u; i < KMS_MAX_BOS; i++) {
		kms_bo_t *b = &srv.bos[i];
		if (b->used && b->handle_open && (b->handle == handle) && (b->owner == client)) {
			return b;
		}
	}
	return NULL;
}


static int bo_from_slot(kms_bo_t *b, uint32_t w, uint32_t h)
{
	uint32_t s;
	uint64_t pa;
	void *va;

	if ((srv.be->id != KMS_BACKEND_PAN) || (w != srv.fb_w) || (h != srv.fb_h)) {
		return -ENOENT;
	}
	for (s = 1u; s < srv.fb_slots; s++) {   /* never slot 0: fbcon + /dev/fb0 */
		if ((srv.slot_used & (1u << s)) == 0u) {
			break;
		}
	}
	if (s >= srv.fb_slots) {
		return -ENOENT;
	}
	pa = srv.fb_pa + (uint64_t)s * srv.fb_h * srv.fb_pitch;
	if ((pa & (_PAGE_SIZE - 1u)) != 0u) {
		return -ENOENT;   /* a PHYS memref must be page-aligned */
	}
	b->size = KMS_PAGE_ROUND((size_t)srv.fb_pitch * srv.fb_h);
	va = mmap(NULL, b->size, PROT_READ | PROT_WRITE, MAP_UNCACHED | MAP_ANONYMOUS | MAP_PHYSMEM, -1, (off_t)pa);
	if (va == MAP_FAILED) {
		return -ENOMEM;
	}
	b->kind = KMS_BOK_SLOT;
	b->slot = s;
	b->pa = pa;
	b->va = va;
	b->pitch = srv.fb_pitch;
	srv.slot_used |= 1u << s;
	return 0;
}


static int bo_from_pool(kms_bo_t *b, uint32_t w, uint32_t h, uint32_t bpp)
{
	size_t off;
	oid_t oid;
	int rc;

	if (!srv.pool_ok) {
		return -ENOMEM;
	}
	b->pitch = ((w * ((bpp + 7u) / 8u)) + 63u) & ~63u;   /* 64-byte rows [inferred: safe for the HVS] */
	b->size = KMS_PAGE_ROUND((size_t)b->pitch * h);
	rc = pool_find(b->size, &off);
	if (rc != 0) {
		return rc;
	}
	b->kind = KMS_BOK_POOL;
	b->off = off;
	b->pa = srv.pool_pa + off;
	b->va = srv.pool_va + off;
	/* Export BEFORE the handle is announced (E1: an importer that asks first would
	 * create a shadow object; the namespace also refuses atSize). */
	oid.port = srv.buf_port;
	oid.id = b->handle;
	rc = memExport(&oid, (void *)b->va, b->size);
	if (rc != 0) {
		KMS_LOG("bo memExport handle=%u off=0x%zx size=%zu rc=%d", b->handle, off, b->size, rc);
		return rc;
	}
	b->exported = 1;
	srv.st.exports_live++;
	return 0;
}


int kms_bo_create(uint32_t client, const kms_create_dumb_req_t *rq, kms_dumb_resp_t *out)
{
	kms_bo_t *b;
	uint32_t idx;
	int rc;

	if ((rq->width == 0u) || (rq->height == 0u) || (rq->width > 8192u) || (rq->height > 8192u) ||
			((rq->bpp != 32u) && (rq->bpp != 16u) && (rq->bpp != 8u))) {
		return -EINVAL;
	}
	b = bo_slot_free(&idx);
	if (b == NULL) {
		return -ENOSPC;
	}
	memset(b, 0, sizeof(*b));
	b->handle = srv.next_handle++;
	b->owner = client;
	b->w = rq->width;
	b->h = rq->height;
	b->bpp = rq->bpp;

	rc = -ENOENT;
	if (((rq->flags & KMS_DUMB_POOL) == 0u) && (rq->bpp == 32u)) {
		rc = bo_from_slot(b, rq->width, rq->height);
	}
	if (rc != 0) {
		rc = bo_from_pool(b, rq->width, rq->height, rq->bpp);
	}
	if (rc != 0) {
		memset(b, 0, sizeof(*b));
		return rc;
	}
	b->used = 1;
	b->handle_open = 1;
	b->refs = 1u;
	/* Linux zeroes dumb BOs; contiguous pages and old fb slots are not. Uncached
	 * (write-combined) stores: about 1-3 ms for a 1080p buffer [inferred]. */
	memset((void *)b->va, 0, b->size);
	__asm__ volatile("dsb sy" ::: "memory");
	srv.st.bos_live++;

	out->handle = b->handle;
	out->pitch = b->pitch;
	out->size = b->size;
	memref_fill(b, &out->mem);
	if (srv.verbose) {
		KMS_LOG("bo create client=%u handle=%u %ux%ux%u kind=%s pa=0x%llx size=%zu", client, b->handle, b->w, b->h,
			b->bpp, (b->kind == KMS_BOK_POOL) ? "pool" : "slot", (unsigned long long)b->pa, b->size);
	}
	return 0;
}


int kms_bo_map(uint32_t client, uint32_t handle, kms_dumb_resp_t *out)
{
	kms_bo_t *b = kms_bo_get(client, handle);

	if (b == NULL) {
		return -ENOENT;
	}
	out->handle = b->handle;
	out->pitch = b->pitch;
	out->size = b->size;
	memref_fill(b, &out->mem);
	return 0;
}


/* Drop one reference; free on the last. */
void kms_bo_unref(uint32_t idx)
{
	kms_bo_t *b = &srv.bos[idx];
	oid_t oid;

	if (!b->used || (b->refs == 0u)) {
		return;
	}
	if (--b->refs != 0u) {
		return;
	}
	if (b->exported) {
		/* Withdraw the name at once; mappings a client still holds keep the pages
		 * (the window holds a reference on the pool), so a late client write lands
		 * in pool memory - never in memory the kernel recycles. */
		oid.port = srv.buf_port;
		oid.id = b->handle;
		(void)memUnexport(&oid);
		srv.st.exports_live--;
	}
	if (b->kind == KMS_BOK_SLOT) {
		(void)munmap((void *)b->va, b->size);
		srv.slot_used &= ~(1u << b->slot);
	}
	srv.st.bos_live--;
	memset(b, 0, sizeof(*b));
}


int kms_bo_destroy(uint32_t client, uint32_t handle)
{
	kms_bo_t *b = kms_bo_get(client, handle);

	if (b == NULL) {
		return -ENOENT;
	}
	b->handle_open = 0;
	kms_bo_unref((uint32_t)(b - srv.bos));
	return 0;
}


int kms_bo_checksum(uint32_t client, const kms_checksum_req_t *rq, kms_checksum_resp_t *out)
{
	kms_bo_t *b = kms_bo_get(client, rq->handle);
	uint32_t h = 2166136261u, i;

	if ((b == NULL) || ((size_t)rq->offset + rq->len > b->size) || ((rq->offset & 3u) != 0u) || ((rq->len & 3u) != 0u)) {
		return -EINVAL;
	}
	for (i = 0u; i < rq->len; i += 4u) {
		uint32_t w = *(volatile const uint32_t *)(b->va + rq->offset + i);
		h = (h ^ w) * 16777619u;
		if (i == 0u) {
			out->first_word = w;
		}
	}
	out->sum = h;
	return 0;
}


/* ========================================================================= */
/* Framebuffers                                                               */
/* ========================================================================= */

kms_fb_t *kms_fb_lookup(uint32_t fb_id)
{
	uint32_t i;

	if (fb_id == 0u) {
		return NULL;
	}
	for (i = 0u; i < KMS_MAX_FBS; i++) {
		if (srv.fbs[i].used && (srv.fbs[i].id == fb_id)) {
			return &srv.fbs[i];
		}
	}
	return NULL;
}


int kms_fb_add(uint32_t client, const kms_addfb2_req_t *rq, uint32_t *fb_id)
{
	kms_bo_t *b = kms_bo_get(client, rq->handle);
	uint32_t i;
	kms_fb_t *f = NULL;

	if (b == NULL) {
		return -ENOENT;
	}
	if ((rq->format != KMS_FMT_XRGB8888) && (rq->format != KMS_FMT_ARGB8888) && (rq->format != KMS_FMT_XBGR8888) &&
			(rq->format != KMS_FMT_ABGR8888)) {
		return -EINVAL;
	}
	if ((rq->modifier != KMS_MOD_LINEAR) || (rq->width == 0u) || (rq->height == 0u) || (rq->pitch < rq->width * 4u) ||
			((uint64_t)rq->offset + (uint64_t)rq->pitch * rq->height > b->size)) {
		return -EINVAL;
	}
	for (i = 0u; i < KMS_MAX_FBS; i++) {
		if (!srv.fbs[i].used) {
			f = &srv.fbs[i];
			break;
		}
	}
	if (f == NULL) {
		return -ENOSPC;
	}
	memset(f, 0, sizeof(*f));
	f->used = 1;
	f->id = srv.next_fb++;
	f->owner = client;
	f->user_ref = 1;
	f->refs = 1u;
	f->bo = (uint32_t)(b - srv.bos);
	f->w = rq->width;
	f->h = rq->height;
	f->format = rq->format;
	f->pitch = rq->pitch;
	f->offset = rq->offset;
	b->refs++;
	*fb_id = f->id;
	return 0;
}


void kms_fb_ref(kms_fb_t *fb)
{
	if (fb != NULL) {
		fb->refs++;
	}
}


void kms_fb_unref(kms_fb_t *fb)
{
	if ((fb == NULL) || (fb->refs == 0u)) {
		return;
	}
	if (--fb->refs == 0u) {
		uint32_t bo = fb->bo;
		memset(fb, 0, sizeof(*fb));
		kms_bo_unref(bo);
	}
}


/* RMFB: the caller has already taken the framebuffer off every plane. */
int kms_fb_rm(uint32_t client, uint32_t fb_id)
{
	kms_fb_t *f = kms_fb_lookup(fb_id);

	if ((f == NULL) || (f->owner != client) || !f->user_ref) {
		return -ENOENT;
	}
	f->user_ref = 0;
	kms_fb_unref(f);
	return 0;
}


/* Client death: its handles and framebuffers go; buffers still on screen stay
 * alive through the planes' references until the caller restores the display. */
void kms_bo_client_gone(uint32_t client)
{
	uint32_t i;

	for (i = 0u; i < KMS_MAX_FBS; i++) {
		kms_fb_t *f = &srv.fbs[i];
		if (f->used && (f->owner == client) && f->user_ref) {
			f->user_ref = 0;
			kms_fb_unref(f);
		}
	}
	for (i = 0u; i < KMS_MAX_BOS; i++) {
		kms_bo_t *b = &srv.bos[i];
		if (b->used && (b->owner == client) && b->handle_open) {
			b->handle_open = 0;
			kms_bo_unref(i);
		}
	}
	for (i = 0u; i < KMS_MAX_BLOBS; i++) {
		if (srv.blobs[i].used && (srv.blobs[i].owner == client)) {
			memset(&srv.blobs[i], 0, sizeof(srv.blobs[i]));
		}
	}
}


/* ========================================================================= */
/* Property blobs                                                             */
/* ========================================================================= */

uint32_t kms_blob_create(uint32_t owner, const void *data, uint32_t len)
{
	uint32_t i;

	if (len > KMS_BLOB_MAX_LEN) {
		return 0u;
	}
	for (i = 0u; i < KMS_MAX_BLOBS; i++) {
		kms_srvblob_t *b = &srv.blobs[i];
		if (!b->used) {
			b->used = 1;
			b->id = srv.next_blob++;
			b->owner = owner;
			b->len = len;
			memcpy(b->data, data, len);
			return b->id;
		}
	}
	return 0u;
}


kms_srvblob_t *kms_blob_get(uint32_t id)
{
	uint32_t i;

	for (i = 0u; (id != 0u) && (i < KMS_MAX_BLOBS); i++) {
		if (srv.blobs[i].used && (srv.blobs[i].id == id)) {
			return &srv.blobs[i];
		}
	}
	return NULL;
}


int kms_blob_destroy(uint32_t owner, uint32_t id)
{
	kms_srvblob_t *b = kms_blob_get(id);

	if ((b == NULL) || (b->owner == 0u) || (b->owner != owner)) {
		return -ENOENT;
	}
	memset(b, 0, sizeof(*b));
	return 0;
}


/* ========================================================================= */
/* /kmsbuf namespace (its own port: the card port's mtOpen returns per-open ids,  */
/* which would rewrite a buffer descriptor's oid)                                */
/* ========================================================================= */

static kms_bo_t *bo_by_export(uint32_t id)
{
	uint32_t i;

	for (i = 0u; (id != 0u) && (i < KMS_MAX_BOS); i++) {
		if (srv.bos[i].used && srv.bos[i].exported && (srv.bos[i].handle == id)) {
			return &srv.bos[i];
		}
	}
	return NULL;
}


static int client_pid(uint32_t id)
{
	return ((id >= 1u) && (id <= KMS_MAX_CLIENTS) && srv.clients[id - 1u].used) ? srv.clients[id - 1u].pid : -1;
}


/* DRM semantics: a dumb BO is private to its fd until PRIME-exported. Stage A
 * checks that the opener is the owner's process (or the BO was exported) but
 * only LOGS a mismatch (once) instead of refusing: the pid the kernel stamps on
 * a path-resolution message has not been observed on hardware yet. */
static uint32_t bufns_pid_notes;

void kms_bufns_thread(void *arg)
{
	msg_t msg;
	msg_rid_t rid;
	char name[16];
	unsigned long id;
	char *end;
	size_t len;
	kms_bo_t *b;

	(void)arg;
	for (;;) {
		if (msgRecv(srv.buf_port, &msg, &rid) < 0) {
			continue;
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
				id = strtoul(name, &end, 10);
				(void)mutexLock(srv.lock);
				b = (*end == '\0') ? bo_by_export((uint32_t)id) : NULL;
				(void)mutexUnlock(srv.lock);
				if (b == NULL) {
					msg.o.err = -ENOENT;
					break;
				}
				msg.o.lookup.fil.port = srv.buf_port;
				msg.o.lookup.fil.id = id;
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
				else {
					/* In particular atSize: the kernel asks for it only when mmap() finds no
					 * live export under the oid; refusing it makes such an mmap() fail instead
					 * of creating a file-backed object that shadows the name (E1 section 3). */
					msg.o.err = -ENOENT;
				}
				break;

			case mtOpen:
				/* 0, never an id: a positive reply would rewrite the descriptor's oid. */
				(void)mutexLock(srv.lock);
				b = bo_by_export((uint32_t)msg.oid.id);
				if ((msg.oid.id != 0u) && (b == NULL)) {
					msg.o.err = -ENOENT;
				}
				else {
					if ((b != NULL) && (client_pid(b->owner) != msg.pid) && !b->prime && (bufns_pid_notes++ == 0u)) {
						KMS_LOG("srv note: %s/%u opened by pid %d, owner pid %d (allowed in Stage A)", KMS_BUF_NS,
							(unsigned)msg.oid.id, msg.pid, client_pid(b->owner));
					}
					msg.o.err = 0;
				}
				(void)mutexUnlock(srv.lock);
				break;

			case mtClose:
				msg.o.err = 0;
				break;

			default:
				msg.o.err = -ENOSYS;
				break;
		}
		(void)msgRespond(srv.buf_port, &msg, rid);
	}
}
