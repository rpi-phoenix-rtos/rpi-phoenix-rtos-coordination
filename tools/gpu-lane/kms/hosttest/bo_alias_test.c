/*
 * Phoenix-RTOS
 *
 * rpi4-kms host test - PRIME import of another client's /kmsbuf export (the
 * alias, kms_bo.c import_alias): the real kms_bo.c against stand-ins for the
 * Phoenix calls (hosttest/shim), no Pi.
 *
 * The rows follow what labwc's GLES2 renderer does on the Pi (m7i arm B): client 2
 * (the renderer's own card0 descriptor, through Mesa kmsro) creates dumb BOs and
 * PRIME-exports them; client 1 (the DRM backend) imports them and adds a
 * framebuffer. Then the lifetime rules: the pages and the /kmsbuf name stay while
 * any handle or framebuffer references them, whoever closes first, and everything
 * is released (bos_live 0, every export withdrawn once) at the end.
 *
 * Negative control (run.sh): the same test linked with the G7 kms_bo.c (the
 * rpi4-kms-g7 source, commit b5386948a) must FAIL at the foreign import.
 *
 * Copyright 2026 Phoenix Systems
 * Author: Witold Bołt
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kms.h"

kms_srv_t srv;
uint64_t kms_cnt_hz = 54000000u;

static const kms_backend_t backend_plane = { .name = "plane", .id = KMS_BACKEND_PLANE };

static int checks, fails;
static char last_log[512];
static uint32_t unexports, unexport_last;


/* ---- stand-ins for the Phoenix calls kms_bo.c makes ---- */

void kms_log(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	(void)vsnprintf(last_log, sizeof(last_log), fmt, ap);
	va_end(ap);
	printf("KMS %s\n", last_log);
}

int memExport(oid_t *oid, void *va, size_t size)
{
	(void)oid;
	(void)va;
	(void)size;
	return 0;
}

int memUnexport(oid_t *oid)
{
	unexports++;
	unexport_last = (uint32_t)oid->id;
	return 0;
}

int mutexLock(handle_t h)
{
	(void)h;
	return 0;
}

int mutexUnlock(handle_t h)
{
	(void)h;
	return 0;
}

int msgRecv(uint32_t port, msg_t *m, msg_rid_t *rid)
{
	(void)port;
	(void)m;
	(void)rid;
	return -ENOSYS;
}

int msgRespond(uint32_t port, msg_t *m, msg_rid_t rid)
{
	(void)port;
	(void)m;
	(void)rid;
	return -ENOSYS;
}

int lookup(const char *name, oid_t *file, oid_t *dev)
{
	(void)name;
	(void)file;
	(void)dev;
	return -ENOENT;
}

addr_t va2pa(void *va)
{
	return (addr_t)srv.pool_pa + (addr_t)((uint8_t *)va - (uint8_t *)srv.pool_va);
}


/* ---- helpers ---- */

static void expect(const char *what, long long got, long long want)
{
	int ok = (got == want);

	checks++;
	if (!ok) {
		fails++;
	}
	printf("KMSHOST alias %-52s got=%-10lld want=%-10lld %s\n", what, got, want, ok ? "ok" : "FAIL");
}

static void expect_log(const char *what, const char *needle)
{
	int ok = (strstr(last_log, needle) != NULL);

	checks++;
	if (!ok) {
		fails++;
	}
	printf("KMSHOST alias %-52s log has \"%s\" %s\n", what, needle, ok ? "ok" : "FAIL");
}

static uint32_t create(uint32_t client)
{
	kms_create_dumb_req_t rq;
	kms_dumb_resp_t r;

	memset(&rq, 0, sizeof(rq));
	rq.width = 1920u;
	rq.height = 1080u;
	rq.bpp = 32u;
	memset(&r, 0, sizeof(r));
	return (kms_bo_create(client, &rq, &r) == 0) ? r.handle : 0u;
}

/* KMS_OP_PRIME_EXPORT as kms_main.c answers it */
static int prime_export(uint32_t client, uint32_t handle)
{
	kms_bo_t *b = kms_bo_get(client, handle);

	if (b == NULL) {
		return -ENOENT;
	}
	b->prime = 1;
	return 0;
}

/* KMS_OP_PRIME_IMPORT of a /kmsbuf name, as op_prime_import runs it (0 = answered) */
static int import(uint32_t client, uint32_t port, uint64_t id, kms_dumb_resp_t *r)
{
	kms_prime_import_req_t rq;

	memset(&rq, 0, sizeof(rq));
	memset(r, 0, sizeof(*r));
	rq.port = port;
	rq.cache = KMS_CACHE_UNCACHED;
	rq.id = id;
	rq.size = 0u;   /* another process exported it: the server knows the size */
	rq.ns = KMS_IMPORT_NS_KMSBUF;
	return kms_import_lookup(client, &rq, r);
}

static int addfb(uint32_t client, uint32_t handle, uint64_t modifier, uint32_t *fb_id)
{
	kms_addfb2_req_t rq;

	memset(&rq, 0, sizeof(rq));
	rq.width = 1920u;
	rq.height = 1080u;
	rq.format = KMS_FMT_XRGB8888;
	rq.handle = handle;
	rq.pitch = 7680u;
	rq.modifier = modifier;
	return kms_fb_add(client, &rq, fb_id);
}

static const kms_bo_t *by_handle(uint32_t handle)
{
	uint32_t i;

	for (i = 0u; i < KMS_MAX_BOS; i++) {
		if (srv.bos[i].used && (srv.bos[i].handle == handle)) {
			return &srv.bos[i];
		}
	}
	return NULL;
}

static long long refs_of(uint32_t handle)
{
	const kms_bo_t *b = by_handle(handle);

	return (b != NULL) ? (long long)b->refs : -1;
}


int main(void)
{
	enum { POOL_MIB = 32 };
	kms_dumb_resp_t r, r2;
	uint32_t src, src2, alias, fb = 0u, fb2 = 0u, other;
	void *pool = NULL;
	size_t off_src;
	int rc;

	if (posix_memalign(&pool, _PAGE_SIZE, (size_t)POOL_MIB * KMS_MIB) != 0) {
		return 2;
	}
	memset(&srv, 0, sizeof(srv));
	srv.be = &backend_plane;
	srv.buf_port = 26u;
	srv.pool_va = pool;
	srv.pool_pa = 0x08000000u;
	srv.pool_size = (size_t)POOL_MIB * KMS_MIB;
	srv.pool_ok = 1;
	srv.next_handle = 3u;   /* the m7i log's first id */
	srv.next_fb = 1u;
	srv.next_blob = 1u;

	/* --- m7i arm B: client 2 allocates and exports, client 1 imports --- */
	src = create(2u);
	expect("client 2 CREATE_DUMB 1920x1080 (handle)", src, 3);
	expect("client 2 PRIME_EXPORT", prime_export(2u, src), 0);
	if (by_handle(src) == NULL) {
		return 2;
	}
	off_src = by_handle(src)->off;

	rc = import(1u, srv.buf_port, src, &r);
	expect("client 1 import of client 2's /kmsbuf/3 (rc)", rc, 0);   /* g7: -EINVAL foreign_kmsbuf */
	alias = r.handle;
	expect("  a new handle of client 1", (alias != 0u) && (alias != src), 1);
	expect("  memref = the exporter's name (kind OID)", r.mem.kind, KMS_MEM_OID);
	expect("  memref port = /kmsbuf", r.mem.port, 26);
	expect("  memref id = the exporter's handle", (long long)r.mem.addr, src);
	expect("  size = the BO's (7680 x 1080)", (long long)r.size, 8294400);
	expect("  pitch = the BO's (7680)", r.pitch, 7680);
	expect_log("  logged as an alias", "alias=1");
	expect("  the source holds the alias's reference (refs)", refs_of(src), 2);
	expect("  bos_live", srv.st.bos_live, 2);

	rc = import(1u, srv.buf_port, src, &r2);
	expect("re-import by client 1: rc", rc, 0);
	expect("  the same handle (DRM)", r2.handle, alias);
	expect("  no extra reference on the source", refs_of(src), 2);
	expect("  no extra BO", srv.st.bos_live, 2);

	rc = import(2u, srv.buf_port, src, &r2);
	expect("client 2 imports its own export: the original handle", (rc == 0) ? (long long)r2.handle : rc, src);

	expect("MAP_DUMB of the alias: rc", kms_bo_map(1u, alias, &r2), 0);
	expect("  maps the exporter's name", (long long)r2.mem.addr, src);
	expect("client 2 cannot use client 1's alias handle", kms_bo_map(2u, alias, &r2), -ENOENT);

	/* ADDFB2 of the alias: the import layout rules, then a framebuffer */
	expect("ADDFB2 alias, Broadcom UIF modifier: refused", addfb(1u, alias, 0x0700000000000006ull, &fb), -EINVAL);
	expect_log("  tagged why=modifier", "why=modifier");
	expect("ADDFB2 alias, LINEAR", addfb(1u, alias, KMS_MOD_LINEAR, &fb), 0);
	expect("  alias refs = handle + framebuffer", refs_of(alias), 2);
	expect("  the framebuffer scans the source's pages",
		(kms_fb_lookup(fb) != NULL) ? (long long)srv.bos[kms_fb_lookup(fb)->bo].pa : -1,
		(long long)(srv.pool_pa + off_src));

	/* the exporter goes first: renderer closed / process died */
	kms_bo_client_gone(2u);
	expect("client 2 gone: the source is still allocated", by_handle(src) != NULL, 1);
	expect("  and still exported (MAP_DUMB of the alias keeps working)",
		(by_handle(src) != NULL) && by_handle(src)->exported, 1);
	expect("  no memUnexport yet", unexports, 0);
	other = create(3u);
	expect("a new BO does not overlap the aliased pages", (other != 0u) && (by_handle(other)->off != off_src), 1);
	expect("client 3 destroys it", kms_bo_destroy(3u, other), 0);
	unexports = 0u;

	/* refusals */
	expect("import of a BO that was never PRIME-exported: -EACCES",
		import(1u, srv.buf_port, (other = create(3u)), &r2), -EACCES);
	expect_log("  tagged why=not_prime", "why=not_prime");
	(void)kms_bo_destroy(3u, other);
	expect("import of an id that is not exported: -ENOENT", import(1u, srv.buf_port, 999u, &r2), -ENOENT);
	expect("import naming another port: -EINVAL", import(1u, 27u, src, &r2), -EINVAL);
	unexports = 0u;

	/* release in DRM order: RMFB, then the handle */
	expect("RMFB of the alias's framebuffer", kms_fb_rm(1u, fb), 0);
	expect("  alias refs = handle", refs_of(alias), 1);
	expect("  source still exported", (by_handle(src) != NULL) && by_handle(src)->exported, 1);
	expect("DESTROY_DUMB of the alias", kms_bo_destroy(1u, alias), 0);
	expect("  the alias is gone", by_handle(alias) == NULL, 1);
	expect("  the source is gone (last reference)", by_handle(src) == NULL, 1);
	expect("  its name withdrawn once", unexports, 1);
	expect("  the withdrawn name is /kmsbuf/3", unexport_last, src);
	expect("  bos_live", srv.st.bos_live, 0);
	expect("  exports_live", srv.st.exports_live, 0);
	expect("  aliases_live", srv.aliases_live, 0);

	/* --- a framebuffer outlives both handles: the plane still shows it --- */
	unexports = 0u;
	src2 = create(2u);
	(void)prime_export(2u, src2);
	expect("second buffer: import (rc)", import(1u, srv.buf_port, src2, &r), 0);
	alias = r.handle;
	expect("ADDFB2 of it", addfb(1u, alias, KMS_MOD_LINEAR, &fb2), 0);
	kms_fb_ref(kms_fb_lookup(fb2));   /* a plane shows it (the commit's reference) */
	expect("client 1 closes the alias handle", kms_bo_destroy(1u, alias), 0);
	expect("client 2 closes the source handle", kms_bo_destroy(2u, src2), 0);
	expect("  both BOs alive for the framebuffer", (by_handle(alias) != NULL) && (by_handle(src2) != NULL), 1);
	expect("  the source's pages are not free (no overlap for a new BO)",
		((other = create(3u)) != 0u) && (by_handle(src2) != NULL) && (by_handle(other)->off != by_handle(src2)->off),
		1);
	(void)kms_bo_destroy(3u, other);
	unexports = 0u;
	expect("  source exported until then", (by_handle(src2) != NULL) && by_handle(src2)->exported, 1);
	kms_bo_client_gone(1u);   /* RMFB by the client's death; the plane keeps its reference */
	expect("client 1 gone: framebuffer kept by the plane", kms_fb_lookup(fb2) != NULL, 1);
	kms_fb_unref(kms_fb_lookup(fb2));   /* the flip away from it completed at a vblank (NULL: no-op) */
	expect("plane released: alias freed", by_handle(alias) == NULL, 1);
	expect("  source freed", by_handle(src2) == NULL, 1);
	expect("  its name withdrawn once", unexports, 1);
	expect("  bos_live", srv.st.bos_live, 0);
	expect("  exports_live", srv.st.exports_live, 0);
	expect("  aliases_live", srv.aliases_live, 0);
	expect("  the pool is whole again (first fit at offset 0)",
		((other = create(3u)) != 0u) && (by_handle(other)->off == 0u), 1);

	printf("KMSHOST RESULT alias checks=%d fails=%d verdict=%s\n", checks, fails, (fails == 0) ? "PASS" : "FAIL");
	free(pool);
	return (fails == 0) ? 0 : 1;
}
