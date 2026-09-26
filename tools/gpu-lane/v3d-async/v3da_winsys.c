/*
 * Phoenix-RTOS
 *
 * Raspberry Pi 4 (BCM2711) V3D 4.2 asynchronous render server - Mesa adapter
 *
 * A drop-in replacement for the in-process winsys objects of libv3d-phoenix.a
 * (mesa/v3d_phoenix_winsys.o, v3d_phoenix_power.o, v3d_libdrm_shim.o) that
 * forwards everything to rpi4-v3d-async (/dev/v3d-async) through libv3da-client.
 * It provides exactly the symbols those three objects export and the rest of the
 * archive, libGL-phoenix.a and the SDL2 glue reference:
 *
 *   phoenix_v3d_ioctl()      - the DRM_IOCTL_V3D_* / GEM_CLOSE entry Mesa's
 *                              libdrm shim (xf86drm.h drmIoctl) calls
 *   drmSyncobj*()            - REAL syncobjs now (the old shim reported every
 *                              fence signalled, true only for a synchronous
 *                              submit), plus sync-file emulation
 *   drmPrime*, drmGetCap, drmGetRenderDeviceNameFromFd
 *   v3d_phoenix_scanout_*, _set_next_scanout, _peek_next_scanout, _flip, ...
 *                            - the transitional present family the SDL2 glue
 *                              uses (firmware pan, performed by the server)
 *   v3d_phoenix_powerOn/_reset/_fb_flip/_fb_virtual_height/_logColdState
 *                            - the server owns power; these are inert or forward
 *
 * Three silent-correctness traps of the Mesa interface are handled here:
 *   1. WAIT_BO must answer "not yet" as `errno = ETIME; return -1` (Mesa's
 *      v3d_wait_bo_ioctl decodes only -1/errno and v3d_bo_wait aborts on any
 *      other error), and it must really wait: v3d_bo_map() relies on it before
 *      every CPU access of a BO that a job may still use.
 *   2. drmSyncobjWait takes an ABSOLUTE CLOCK_MONOTONIC deadline in ns
 *      (os_time_get_absolute_timeout; INT64_MAX = forever).
 *   3. glFinish needs a fence fd: the Phoenix Mesa patch in v3d_pipe_flush turns
 *      an exported fd of -1 into a NULL fence, and st_finish then does not wait at
 *      all. drmSyncobjExportSyncFile therefore returns a real descriptor (a dup of
 *      the device fd, which Mesa close()s) mapped to a fence snapshot, and
 *      drmSyncobjImportSyncFile attaches that fence to a syncobj in the server.
 *
 * Fast paths (no IPC): WAIT_BO and drmSyncobjWait on fences that already passed
 * read the shared fence page; this client mirrors its own syncobjs' fences and
 * every BO's last-use fence (from its own submits).
 *
 * M1 limits (documented in docs/gpu-new-lane/M1-async-render-server.md): implicit
 * sync on a BO covers this process's submits only (single GL client per server
 * in M1); GEM_OPEN/FLINK/PRIME are not provided.
 *
 * Copyright 2026 Phoenix Systems
 * Author: Witold Bołt
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/mman.h>

#include "v3d_drm.h"         /* DRM_V3D_*, drm_v3d_* (vendored MIT uapi); no <sys/ioctl.h> in this TU */
#include "libv3da-client.h"


#define V3DA_HANDLE_SLOT_BITS_CLIENT 13u   /* == V3DA_HANDLE_SLOT_BITS (server, v3da.h) */
#define A_MAX_BOS      (1u << V3DA_HANDLE_SLOT_BITS_CLIENT)
#define A_MAX_SYNC     256u
#define A_MAX_SYNCFD   64u
#define A_NPARAM       32u
#define FOREVER_NS     (-1LL)

/* Declarations for the symbols this file exports (the glue and Mesa declare
 * them extern at their call sites; there is no shared header). */
int phoenix_v3d_ioctl(int fd, unsigned long request, void *arg);
int drmSyncobjCreate(int fd, uint32_t flags, uint32_t *handle);
int drmSyncobjDestroy(int fd, uint32_t handle);
int drmSyncobjWait(int fd, uint32_t *handles, unsigned num_handles, int64_t timeout_nsec, unsigned flags,
	uint32_t *first_signaled);
int drmSyncobjImportSyncFile(int fd, uint32_t handle, int sync_file_fd);
int drmSyncobjExportSyncFile(int fd, uint32_t handle, int *sync_file_fd);
int drmPrimeHandleToFD(int fd, uint32_t handle, uint32_t flags, int *prime_fd);
int drmPrimeFDToHandle(int fd, int prime_fd, uint32_t *handle);
int drmGetCap(int fd, uint64_t capability, uint64_t *value);
char *drmGetRenderDeviceNameFromFd(int fd);
int v3d_phoenix_scanout_init(uint32_t pa, uint32_t w, uint32_t h, uint32_t pitch);
void v3d_phoenix_set_scanout(uint32_t pa, uint32_t bytes);
int v3d_phoenix_scanout_active(void);
int v3d_phoenix_scanout_double(void);
int v3d_phoenix_scanout_nbuf(void);
void v3d_phoenix_set_next_scanout(void);
int v3d_phoenix_peek_next_scanout(void);
void v3d_phoenix_flip(int buf);
unsigned long v3d_phoenix_flip_count(void);
uint32_t v3d_phoenix_scanout_readback(void *dst, int buf, uint32_t bytes);
void v3d_phoenix_harness_reset(void);
uint32_t v3d_phoenix_last_bin_crc(uint32_t *qma, uint32_t *qms, uint32_t *crc);
int v3d_phoenix_powerOn(void);
int v3d_phoenix_reset(void);
void v3d_phoenix_fb_flip(unsigned yoff);
unsigned v3d_phoenix_fb_virtual_height(void);
void v3d_phoenix_logColdState(void);


typedef struct {
	uint32_t handle;          /* 0 = free */
	uint32_t gpuva;
	uint32_t size;
	int scanout;              /* 1 + fb buffer index, 0 = normal */
	void *cpu;
	v3da_memref_t mem;
	v3da_fence_t last;        /* last job of ours that used it (seqno 0 = none) */
} a_bo_t;

typedef struct {
	uint32_t handle;          /* 0 = free */
	int state;                /* enum v3da_syncobj_state (mirror of the server's) */
	v3da_fence_t fence;
} a_sync_t;

static struct {
	pthread_mutex_t lock;
	int tried;
	int rc;                   /* connect result */
	v3da_conn_t conn;

	a_bo_t bo[A_MAX_BOS];
	a_sync_t sync[A_MAX_SYNC];
	struct {
		int fd;
		v3da_fence_t fence;
	} sf[A_MAX_SYNCFD];
	uint32_t sf_next;

	uint64_t param[A_NPARAM];
	uint8_t have_param[A_NPARAM];

	/* present */
	uint32_t scan_pa, scan_bytes, scan_h, scan_virt_h, scan_nbuf;
	uint32_t scan_claims;
	int next_scanout;
	uint32_t shown;
	v3da_fence_t last_render;   /* the fence a flip waits for */
	volatile uint8_t *scan_cpu;

	/* statistics (flipstat window) */
	unsigned long flips, win_flips;
	uint64_t win_t0, first_t;
	int flipstat;               /* 0 = unread, 1 = on, 2 = off */
	unsigned flipstat_ms;
	uint32_t n_cl, n_tfu, n_csd, n_create, n_waits_ipc;
	uint64_t wait_us;           /* time inside WAIT_BO / drmSyncobjWait / fence waits */
} A;   /* all-zero on purpose: .bss, not ~600 KiB of .data in every game binary
         * (PTHREAD_MUTEX_INITIALIZER is { 0, 0 } in libphoenix) */


static uint64_t now_us(void)
{
	struct timespec ts;

	(void)clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}


static int64_t now_ns(void)
{
	struct timespec ts;

	(void)clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}


/* libdrm convention: -1 with errno. */
static int fail(int negerr)
{
	errno = -negerr;
	return -1;
}


/* Lazy connect (locked). Every entry point needs the server; a missing server is
 * reported once and every call fails with ENODEV. */
static int conn_locked(void)
{
	if (A.tried == 0) {
		A.tried = 1;
		A.rc = v3da_connect(&A.conn);
		if (A.rc != 0) {
			fprintf(stderr, "v3da-winsys: /dev/" V3DA_DEV_NAME " unavailable (rc=%d) - is rpi4-v3d-async running? "
				"GPU calls fail with ENODEV\n", A.rc);
		}
		else {
			fprintf(stderr, "v3da-winsys: connected to rpi4-v3d-async pid=%u client=%u slot=%u proto=%u (new GPU lane)\n",
				A.conn.hello.server_pid, A.conn.hello.client_id, A.conn.hello.slot, A.conn.hello.proto);
		}
	}
	return (A.rc == 0) ? 0 : -ENODEV;
}


static int conn_get(void)
{
	int rc;

	pthread_mutex_lock(&A.lock);
	rc = conn_locked();
	pthread_mutex_unlock(&A.lock);
	return rc;
}


static a_bo_t *bo_slot(uint32_t handle)
{
	uint32_t s = handle & (A_MAX_BOS - 1u);

	if ((handle == 0u) || (s == 0u)) {
		return NULL;
	}
	return &A.bo[s - 1u];
}


static a_bo_t *bo_get(uint32_t handle)
{
	a_bo_t *b = bo_slot(handle);

	return ((b != NULL) && (b->handle == handle)) ? b : NULL;
}


static a_sync_t *sync_get(uint32_t handle)
{
	uint32_t i;

	for (i = 0u; i < A_MAX_SYNC; i++) {
		if ((A.sync[i].handle == handle) && (handle != 0u)) {
			return &A.sync[i];
		}
	}
	return NULL;
}


/* Wait for one fence: fast path, then bounded server waits (library). */
static int fence_wait_timed(const v3da_fence_t *f, int64_t rel_ns)
{
	uint64_t t0;
	uint32_t ipc0;
	int rc;

	if ((f->seqno == 0u) || (v3da_fence_signaled(&A.conn, f) != 0)) {
		return 0;
	}
	if (rel_ns == 0) {
		return -ETIMEDOUT;
	}
	t0 = now_us();
	ipc0 = A.conn.ipc_waits;
	rc = v3da_fence_wait(&A.conn, f, rel_ns);
	A.wait_us += now_us() - t0;
	A.n_waits_ipc += A.conn.ipc_waits - ipc0;
	return rc;
}


/* ========================================================================= */
/* phoenix_v3d_ioctl                                                           */
/* ========================================================================= */

static int ioc_get_param(struct drm_v3d_get_param *gp)
{
	uint64_t v = 0u;
	int rc;

	pthread_mutex_lock(&A.lock);
	if ((gp->param < A_NPARAM) && (A.have_param[gp->param] != 0u)) {
		gp->value = A.param[gp->param];
		pthread_mutex_unlock(&A.lock);
		return 0;
	}
	pthread_mutex_unlock(&A.lock);
	rc = v3da_get_param(&A.conn, gp->param, &v);
	if (rc != 0) {
		return fail(rc);
	}
	gp->value = v;
	pthread_mutex_lock(&A.lock);
	if (gp->param < A_NPARAM) {
		A.param[gp->param] = v;
		A.have_param[gp->param] = 1u;
	}
	pthread_mutex_unlock(&A.lock);
	return 0;
}


static int ioc_create_bo(struct drm_v3d_create_bo *c)
{
	v3da_bo_create_resp_t r;
	uint32_t flags = 0u;
	a_bo_t *b;
	void *cpu;
	int rc;

	pthread_mutex_lock(&A.lock);
	if ((c->flags & 0x1u) != 0u) {
		flags |= V3DA_BO_CACHEABLE;
	}
	if (((c->flags & 0x2u) != 0u) || (A.next_scanout != 0)) {
		flags |= V3DA_BO_SCANOUT;
	}
	A.next_scanout = 0;   /* one-shot, consumed by this allocation whatever happens */
	pthread_mutex_unlock(&A.lock);

	rc = v3da_bo_create(&A.conn, c->size, flags, &r);
	if (rc != 0) {
		return fail(rc);
	}
	A.n_create++;
	cpu = v3da_map(&r.mem, 1);
	if (cpu == NULL) {
		(void)v3da_bo_close(&A.conn, r.handle);
		return fail(-ENOMEM);
	}
	pthread_mutex_lock(&A.lock);
	b = bo_slot(r.handle);
	if (b == NULL) {
		pthread_mutex_unlock(&A.lock);
		v3da_unmap(cpu, &r.mem);
		(void)v3da_bo_close(&A.conn, r.handle);
		return fail(-EINVAL);
	}
	if ((b->handle != 0u) && (b->cpu != NULL)) {
		v3da_unmap(b->cpu, &b->mem);   /* a stale entry of a handle closed behind our back */
	}
	memset(b, 0, sizeof(*b));
	b->handle = r.handle;
	b->gpuva = r.gpuva;
	b->size = r.size;
	b->scanout = (int)r.scanout;
	b->cpu = cpu;
	b->mem = r.mem;
	if (r.scanout != 0u) {
		A.scan_claims++;
		fprintf(stderr, "v3da-winsys: RT scanout buf%u handle=0x%x gpuva=0x%x size=%u\n", r.scanout - 1u, r.handle,
			r.gpuva, r.size);
	}
	pthread_mutex_unlock(&A.lock);
	c->handle = r.handle;
	c->offset = r.gpuva;
	return 0;
}


static int ioc_close_bo(struct drm_gem_close *gc)
{
	a_bo_t *b;
	void *cpu = NULL;
	v3da_memref_t mem;

	memset(&mem, 0, sizeof(mem));
	pthread_mutex_lock(&A.lock);
	b = bo_get(gc->handle);
	if (b != NULL) {
		cpu = b->cpu;
		mem = b->mem;
		if ((b->scanout != 0) && (A.scan_claims > 0u)) {
			A.scan_claims--;
		}
		memset(b, 0, sizeof(*b));
	}
	pthread_mutex_unlock(&A.lock);
	if (cpu != NULL) {
		v3da_unmap(cpu, &mem);
	}
	/* The server keeps the BO alive while a job still uses it (in-flight pin),
	 * then quarantines it. */
	return (v3da_bo_close(&A.conn, gc->handle) == 0) ? 0 : fail(-EINVAL);
}


static int ioc_mmap_bo(struct drm_v3d_mmap_bo *m)
{
	v3da_bo_resp_t r;
	a_bo_t *b;
	void *cpu;
	int rc;

	/* Mesa uses the returned offset as the CPU pointer (Phoenix patch in
	 * v3d_bo_map_unsynchronized): return our MAP_PHYSMEM view. */
	pthread_mutex_lock(&A.lock);
	b = bo_get(m->handle);
	if ((b != NULL) && (b->cpu != NULL)) {
		m->offset = (uint64_t)(uintptr_t)b->cpu;
		pthread_mutex_unlock(&A.lock);
		return 0;
	}
	pthread_mutex_unlock(&A.lock);

	rc = v3da_bo_mmap(&A.conn, m->handle, &r);   /* a BO this client did not create */
	if (rc != 0) {
		return fail(rc);
	}
	cpu = v3da_map(&r.mem, 1);
	if (cpu == NULL) {
		return fail(-ENOMEM);
	}
	pthread_mutex_lock(&A.lock);
	b = bo_slot(m->handle);
	if ((b != NULL) && (b->handle == 0u)) {
		b->handle = m->handle;
		b->gpuva = r.gpuva;
		b->size = r.size;
		b->cpu = cpu;
		b->mem = r.mem;
	}
	pthread_mutex_unlock(&A.lock);
	m->offset = (uint64_t)(uintptr_t)cpu;
	return 0;
}


static int ioc_get_bo_offset(struct drm_v3d_get_bo_offset *g)
{
	a_bo_t *b;
	uint32_t va = 0u;
	int rc;

	pthread_mutex_lock(&A.lock);
	b = bo_get(g->handle);
	if (b != NULL) {
		g->offset = b->gpuva;
		pthread_mutex_unlock(&A.lock);
		return 0;
	}
	pthread_mutex_unlock(&A.lock);
	rc = v3da_bo_offset(&A.conn, g->handle, &va);
	if (rc != 0) {
		return fail(rc);
	}
	g->offset = va;
	return 0;
}


/* DRM WAIT_BO: timeout_ns is RELATIVE here (unlike drmSyncobjWait); Mesa passes
 * OS_TIMEOUT_INFINITE (~0) or 0 (the BO cache's "is it idle" probe). */
static int ioc_wait_bo(struct drm_v3d_wait_bo *w)
{
	v3da_fence_t f;
	a_bo_t *b;
	int64_t rel;
	int rc, known;

	pthread_mutex_lock(&A.lock);
	b = bo_get(w->handle);
	known = (b != NULL) ? 1 : 0;
	if (b != NULL) {
		f = b->last;
	}
	pthread_mutex_unlock(&A.lock);

	rel = (w->timeout_ns >= (uint64_t)INT64_MAX) ? FOREVER_NS : (int64_t)w->timeout_ns;
	if (known != 0) {
		rc = fence_wait_timed(&f, rel);
	}
	else {
		/* Not ours: the server knows every client's last use. */
		uint64_t t0 = now_us();
		rc = v3da_bo_wait(&A.conn, w->handle, rel);
		A.wait_us += now_us() - t0;
		A.n_waits_ipc++;
	}
	if (rc == -ETIMEDOUT) {
		return fail(-ETIME);
	}
	return (rc == 0) ? 0 : fail(rc);
}


/* Flatten the syncobj arguments of a submit: the legacy single in/out fields,
 * or a DRM_V3D_EXT_ID_MULTI_SYNC extension when the submit carries one. */
static int collect_sems(uint32_t flags, uint64_t extensions, const uint32_t *legacy_in, const uint32_t *legacy_in_flags,
	uint32_t nlegacy_in, uint32_t legacy_out, int is_cl, v3da_sem_t *in, uint32_t *nin, v3da_sem_t *out, uint32_t *nout)
{
	const struct drm_v3d_extension *ext;
	const struct drm_v3d_multi_sync *ms;
	const struct drm_v3d_sem *sems;
	uint32_t i;

	*nin = 0u;
	*nout = 0u;
	if (((flags & DRM_V3D_SUBMIT_EXTENSION) != 0u) && (extensions != 0u)) {
		for (ext = (const struct drm_v3d_extension *)(uintptr_t)extensions; ext != NULL;
				ext = (const struct drm_v3d_extension *)(uintptr_t)ext->next) {
			if (ext->id != DRM_V3D_EXT_ID_MULTI_SYNC) {
				return -EINVAL;   /* CPU-queue extensions: not in M1 */
			}
			ms = (const struct drm_v3d_multi_sync *)ext;
			if ((ms->in_sync_count > V3DA_SUBMIT_MAX_SEMS) || (ms->out_sync_count > V3DA_SUBMIT_MAX_SEMS)) {
				return -EINVAL;
			}
			sems = (const struct drm_v3d_sem *)(uintptr_t)ms->in_syncs;
			for (i = 0u; i < ms->in_sync_count; i++) {
				in[*nin].handle = sems[i].handle;
				in[*nin].flags = ((is_cl != 0) && (ms->wait_stage == V3D_RENDER)) ? V3DA_SEM_RENDER : 0u;
				(*nin)++;
			}
			sems = (const struct drm_v3d_sem *)(uintptr_t)ms->out_syncs;
			for (i = 0u; i < ms->out_sync_count; i++) {
				out[*nout].handle = sems[i].handle;
				out[*nout].flags = 0u;
				(*nout)++;
			}
			return 0;
		}
	}
	for (i = 0u; i < nlegacy_in; i++) {
		if (legacy_in[i] != 0u) {
			in[*nin].handle = legacy_in[i];
			in[*nin].flags = legacy_in_flags[i];
			(*nin)++;
		}
	}
	if (legacy_out != 0u) {
		out[0].handle = legacy_out;
		out[0].flags = 0u;
		*nout = 1u;
	}
	return 0;
}


/* After a successful submit: mirror the out-syncobjs, record the BOs' last use. */
static void after_submit(const uint32_t *bos, uint32_t nbo, const v3da_sem_t *out, uint32_t nout,
	const v3da_submit_resp_t *r, int is_render)
{
	a_sync_t *s;
	a_bo_t *b;
	uint32_t i;

	pthread_mutex_lock(&A.lock);
	for (i = 0u; i < nbo; i++) {
		b = bo_get(bos[i]);
		if (b != NULL) {
			b->last = r->last;
		}
	}
	for (i = 0u; i < nout; i++) {
		s = sync_get(out[i].handle);
		if (s != NULL) {
			s->state = V3DA_SYNC_FENCE;
			s->fence = r->last;
		}
	}
	if (is_render != 0) {
		A.last_render = r->last;
	}
	pthread_mutex_unlock(&A.lock);
}


static int ioc_submit_cl(const struct drm_v3d_submit_cl *s)
{
	v3da_cl_desc_t d;
	v3da_sem_t in[V3DA_SUBMIT_MAX_SEMS], out[V3DA_SUBMIT_MAX_SEMS];
	v3da_submit_resp_t r;
	uint32_t nin, nout, lin[2], lfl[2];
	const uint32_t *bos = (const uint32_t *)(uintptr_t)s->bo_handles;
	int rc;

	d.bcl_start = s->bcl_start;
	d.bcl_end = s->bcl_end;
	d.rcl_start = s->rcl_start;
	d.rcl_end = s->rcl_end;
	d.qma = s->qma;
	d.qms = s->qms;
	d.qts = s->qts;
	d.flags = ((s->flags & DRM_V3D_SUBMIT_CL_FLUSH_CACHE) != 0u) ? V3DA_CL_FLUSH_CACHE : 0u;
	lin[0] = s->in_sync_bcl;
	lfl[0] = 0u;
	lin[1] = s->in_sync_rcl;
	lfl[1] = V3DA_SEM_RENDER;
	rc = collect_sems(s->flags, s->extensions, lin, lfl, 2u, s->out_sync, 1, in, &nin, out, &nout);
	if (rc != 0) {
		return fail(rc);
	}
	rc = v3da_submit(&A.conn, V3DA_OP_SUBMIT_CL, &d, sizeof(d), (bos != NULL) ? bos : NULL,
		(bos != NULL) ? s->bo_handle_count : 0u, in, nin, out, nout, &r);
	if (rc != 0) {
		return fail(rc);
	}
	A.n_cl++;
	after_submit(bos, (bos != NULL) ? s->bo_handle_count : 0u, out, nout, &r, 1);
	return 0;
}


static int ioc_submit_tfu(const struct drm_v3d_submit_tfu *t)
{
	v3da_tfu_desc_t d;
	v3da_sem_t in[V3DA_SUBMIT_MAX_SEMS], out[V3DA_SUBMIT_MAX_SEMS];
	v3da_submit_resp_t r;
	uint32_t nin, nout, bos[4], nbo = 0u, i, lfl = 0u;
	int rc;

	d.icfg = t->icfg;
	d.iia = t->iia;
	d.iis = t->iis;
	d.ica = t->ica;
	d.iua = t->iua;
	d.ioa = t->ioa;
	d.ios = t->ios;
	memcpy(d.coef, t->coef, sizeof(d.coef));
	for (i = 0u; i < 4u; i++) {
		if (t->bo_handles[i] != 0u) {
			bos[nbo++] = t->bo_handles[i];
		}
	}
	rc = collect_sems(t->flags, t->extensions, &t->in_sync, &lfl, 1u, t->out_sync, 0, in, &nin, out, &nout);
	if (rc != 0) {
		return fail(rc);
	}
	rc = v3da_submit(&A.conn, V3DA_OP_SUBMIT_TFU, &d, sizeof(d), bos, nbo, in, nin, out, nout, &r);
	if (rc != 0) {
		return fail(rc);
	}
	A.n_tfu++;
	after_submit(bos, nbo, out, nout, &r, 0);
	return 0;
}


static int ioc_submit_csd(const struct drm_v3d_submit_csd *s)
{
	v3da_csd_desc_t d;
	v3da_sem_t in[V3DA_SUBMIT_MAX_SEMS], out[V3DA_SUBMIT_MAX_SEMS];
	v3da_submit_resp_t r;
	uint32_t nin, nout, lfl = 0u;
	const uint32_t *bos = (const uint32_t *)(uintptr_t)s->bo_handles;
	int rc;

	memcpy(d.cfg, s->cfg, sizeof(d.cfg));
	memcpy(d.coef, s->coef, sizeof(d.coef));
	rc = collect_sems(s->flags, s->extensions, &s->in_sync, &lfl, 1u, s->out_sync, 0, in, &nin, out, &nout);
	if (rc != 0) {
		return fail(rc);
	}
	rc = v3da_submit(&A.conn, V3DA_OP_SUBMIT_CSD, &d, sizeof(d), bos, (bos != NULL) ? s->bo_handle_count : 0u,
		in, nin, out, nout, &r);
	if (rc != 0) {
		return fail(rc);
	}
	A.n_csd++;
	after_submit(bos, (bos != NULL) ? s->bo_handle_count : 0u, out, nout, &r, 0);
	return 0;
}


int phoenix_v3d_ioctl(int fd, unsigned long request, void *arg)
{
	/* Mesa builds requests as DRM_IOWR(DRM_COMMAND_BASE + DRM_V3D_*, ...): strip the
	 * base (same as the winsys entry). */
	unsigned cmd = _IOC_NR(request) - DRM_COMMAND_BASE;

	(void)fd;
	if (conn_get() != 0) {
		return fail(-ENODEV);
	}
	if (_IOC_NR(request) == _IOC_NR(DRM_IOCTL_GEM_CLOSE)) {
		return ioc_close_bo(arg);
	}
	switch (cmd) {
		case DRM_V3D_GET_PARAM:     return ioc_get_param(arg);
		case DRM_V3D_CREATE_BO:     return ioc_create_bo(arg);
		case DRM_V3D_MMAP_BO:       return ioc_mmap_bo(arg);
		case DRM_V3D_GET_BO_OFFSET: return ioc_get_bo_offset(arg);
		case DRM_V3D_WAIT_BO:       return ioc_wait_bo(arg);
		case DRM_V3D_SUBMIT_CL:     return ioc_submit_cl(arg);
		case DRM_V3D_SUBMIT_TFU:    return ioc_submit_tfu(arg);
		case DRM_V3D_SUBMIT_CSD:    return ioc_submit_csd(arg);
		default:                    return 0;   /* perfmon etc.: no-op, as the winsys */
	}
}


/* ========================================================================= */
/* libdrm syncobj surface                                                     */
/* ========================================================================= */

int drmSyncobjCreate(int fd, uint32_t flags, uint32_t *handle)
{
	uint32_t h = 0u, i;
	int rc;

	(void)fd;
	if ((handle == NULL) || (conn_get() != 0)) {
		return fail(-EINVAL);
	}
	rc = v3da_syncobj_create(&A.conn, ((flags & 1u) != 0u) ? V3DA_SYNCOBJ_CREATE_SIGNALED : 0u, &h);
	if (rc != 0) {
		return fail(rc);
	}
	pthread_mutex_lock(&A.lock);
	for (i = 0u; i < A_MAX_SYNC; i++) {
		if (A.sync[i].handle == 0u) {
			A.sync[i].handle = h;
			A.sync[i].state = ((flags & 1u) != 0u) ? V3DA_SYNC_SIGNALED : V3DA_SYNC_EMPTY;
			memset(&A.sync[i].fence, 0, sizeof(A.sync[i].fence));
			break;
		}
	}
	pthread_mutex_unlock(&A.lock);
	*handle = h;
	return 0;
}


int drmSyncobjDestroy(int fd, uint32_t handle)
{
	a_sync_t *s;

	(void)fd;
	pthread_mutex_lock(&A.lock);
	s = sync_get(handle);
	if (s != NULL) {
		memset(s, 0, sizeof(*s));
	}
	pthread_mutex_unlock(&A.lock);
	return (v3da_syncobj_destroy(&A.conn, handle) == 0) ? 0 : fail(-EINVAL);
}


int drmSyncobjWait(int fd, uint32_t *handles, unsigned num_handles, int64_t timeout_nsec, unsigned flags,
	uint32_t *first_signaled)
{
	v3da_fence_t f[V3DA_SYNCOBJ_WAIT_MAX];
	int st[V3DA_SYNCOBJ_WAIT_MAX];
	int64_t rel, now, deadline, left;
	uint32_t i, nsig = 0u, first = 0u, sflags = 0u;
	int all = ((flags & 1u) != 0u) ? 1 : 0, known = 1, rc = 0;
	uint64_t t0;
	a_sync_t *s;

	(void)fd;
	if ((handles == NULL) || (num_handles == 0u) || (num_handles > V3DA_SYNCOBJ_WAIT_MAX) || (conn_get() != 0)) {
		return fail(-EINVAL);
	}
	/* Absolute CLOCK_MONOTONIC deadline -> relative. */
	now = now_ns();
	if (timeout_nsec >= INT64_MAX) {
		rel = FOREVER_NS;
		deadline = FOREVER_NS;
	}
	else {
		rel = (timeout_nsec > now) ? (timeout_nsec - now) : 0;
		deadline = now + rel;
	}

	pthread_mutex_lock(&A.lock);
	for (i = 0u; i < num_handles; i++) {
		s = sync_get(handles[i]);
		if (s == NULL) {
			known = 0;
			break;
		}
		st[i] = s->state;
		f[i] = s->fence;
	}
	pthread_mutex_unlock(&A.lock);

	if (known != 0) {
		for (i = 0u; i < num_handles; i++) {
			if ((st[i] == V3DA_SYNC_SIGNALED) || ((st[i] == V3DA_SYNC_FENCE) && (v3da_fence_signaled(&A.conn, &f[i]) != 0))) {
				if (nsig++ == 0u) {
					first = i;
				}
			}
			else if ((st[i] == V3DA_SYNC_EMPTY) && ((flags & 2u) == 0u)) {
				return fail(-EINVAL);   /* DRM: an empty syncobj without WAIT_FOR_SUBMIT */
			}
		}
		if (((all != 0) && (nsig == num_handles)) || ((all == 0) && (nsig != 0u))) {
			if (first_signaled != NULL) {
				*first_signaled = first;
			}
			return 0;
		}
		/* The common case (glFinish, context destroy): one handle, or ALL of them,
		 * each with a known fence - wait the fences directly. */
		if ((all != 0) || (num_handles == 1u)) {
			for (i = 0u; (i < num_handles) && (rc == 0); i++) {
				if (st[i] == V3DA_SYNC_FENCE) {
					left = FOREVER_NS;
					if (deadline >= 0) {
						left = deadline - now_ns();
						if (left < 0) {
							left = 0;
						}
					}
					rc = fence_wait_timed(&f[i], left);
				}
				else if (st[i] == V3DA_SYNC_EMPTY) {
					known = 0;   /* waiting for a submit: let the server do it */
					break;
				}
			}
			if (known != 0) {
				if (rc == -ETIMEDOUT) {
					return fail(-ETIME);
				}
				if ((rc == 0) && (first_signaled != NULL)) {
					*first_signaled = 0u;
				}
				return (rc == 0) ? 0 : fail(rc);
			}
		}
	}

	if (all != 0) {
		sflags |= V3DA_SYNCOBJ_WAIT_ALL;
	}
	if ((flags & 2u) != 0u) {
		sflags |= V3DA_SYNCOBJ_WAIT_FOR_SUBMIT;
	}
	t0 = now_us();
	rc = v3da_syncobj_wait(&A.conn, handles, num_handles, sflags, rel, first_signaled);
	A.wait_us += now_us() - t0;
	A.n_waits_ipc++;
	if (rc == -ETIMEDOUT) {
		return fail(-ETIME);
	}
	return (rc == 0) ? 0 : fail(rc);
}


/* Sync-file emulation. An exported "sync file" is a dup of the device fd (Mesa
 * only ever close()s it or imports it back) plus a fence snapshot. */
int drmSyncobjExportSyncFile(int fd, uint32_t handle, int *sync_file_fd)
{
	v3da_fence_t f;
	a_sync_t *s;
	int nfd;

	(void)fd;
	if ((sync_file_fd == NULL) || (conn_get() != 0)) {
		return fail(-EINVAL);
	}
	*sync_file_fd = -1;
	memset(&f, 0, sizeof(f));
	pthread_mutex_lock(&A.lock);
	s = sync_get(handle);
	if (s == NULL) {
		pthread_mutex_unlock(&A.lock);
		return fail(-EINVAL);
	}
	if (s->state == V3DA_SYNC_FENCE) {
		f = s->fence;   /* seqno 0 = "already signalled" snapshot otherwise */
	}
	nfd = dup(A.conn.fd);
	if (nfd < 0) {
		pthread_mutex_unlock(&A.lock);
		return fail(-EMFILE);
	}
	A.sf[A.sf_next].fd = nfd;
	A.sf[A.sf_next].fence = f;
	A.sf_next = (A.sf_next + 1u) % A_MAX_SYNCFD;
	pthread_mutex_unlock(&A.lock);
	*sync_file_fd = nfd;
	return 0;
}


int drmSyncobjImportSyncFile(int fd, uint32_t handle, int sync_file_fd)
{
	v3da_fence_t f;
	a_sync_t *s;
	uint32_t i;
	int found = 0, rc;

	(void)fd;
	if (conn_get() != 0) {
		return fail(-EINVAL);
	}
	pthread_mutex_lock(&A.lock);
	/* newest first: a recycled fd number must resolve to its latest export */
	for (i = 0u; i < A_MAX_SYNCFD; i++) {
		uint32_t k = (A.sf_next + A_MAX_SYNCFD - 1u - i) % A_MAX_SYNCFD;
		if ((A.sf[k].fd == sync_file_fd) && (sync_file_fd >= 0)) {
			f = A.sf[k].fence;
			found = 1;
			break;
		}
	}
	pthread_mutex_unlock(&A.lock);
	if (found == 0) {
		return fail(-EINVAL);
	}
	rc = v3da_syncobj_import(&A.conn, handle, &f);
	if (rc != 0) {
		return fail(rc);
	}
	pthread_mutex_lock(&A.lock);
	s = sync_get(handle);
	if (s != NULL) {
		s->state = (f.seqno == 0u) ? V3DA_SYNC_SIGNALED : V3DA_SYNC_FENCE;
		s->fence = f;
	}
	pthread_mutex_unlock(&A.lock);
	return 0;
}


/* No buffer sharing through PRIME in M1 (the E1 export is the M3 mechanism). */
int drmPrimeHandleToFD(int fd, uint32_t handle, uint32_t flags, int *prime_fd)
{
	(void)fd;
	(void)handle;
	(void)flags;
	if (prime_fd != NULL) {
		*prime_fd = -1;
	}
	return -1;
}


int drmPrimeFDToHandle(int fd, int prime_fd, uint32_t *handle)
{
	(void)fd;
	(void)prime_fd;
	(void)handle;
	return -1;
}


int drmGetCap(int fd, uint64_t capability, uint64_t *value)
{
	(void)fd;
	(void)capability;
	if (value != NULL) {
		*value = 0u;
	}
	return 0;
}


char *drmGetRenderDeviceNameFromFd(int fd)
{
	(void)fd;
	return NULL;
}


/* ========================================================================= */
/* Transitional present family (SDL2 glue)                                    */
/* ========================================================================= */

int v3d_phoenix_scanout_init(uint32_t pa, uint32_t w, uint32_t h, uint32_t pitch)
{
	v3da_scanout_resp_t r;
	int rc;

	if (conn_get() != 0) {
		return 1;
	}
	memset(&r, 0, sizeof(r));
	rc = v3da_scanout_info(&A.conn, pa, w, h, pitch, &r);
	pthread_mutex_lock(&A.lock);
	if (rc == 0) {
		A.scan_pa = pa;
		A.scan_bytes = r.bytes;
		A.scan_h = h;
		A.scan_virt_h = r.virt_h;
		A.scan_nbuf = r.nbuf;
	}
	pthread_mutex_unlock(&A.lock);
	fprintf(stderr, "v3da-winsys: scanout init pa=0x%08x %ux%u pitch=%u virt_h=%u rc=%d -> %u buffer(s)%s\n", pa, w, h,
		pitch, r.virt_h, rc, r.nbuf, (r.nbuf >= 2u) ? " page-flip (server pans via vcmbox)" : "");
	return (rc == 0) ? (int)r.nbuf : 1;
}


/* Legacy single-buffer entry (not referenced by the current glue). */
void v3d_phoenix_set_scanout(uint32_t pa, uint32_t bytes)
{
	fprintf(stderr, "v3da-winsys: v3d_phoenix_set_scanout(0x%08x, %u) ignored - use v3d_phoenix_scanout_init\n", pa,
		bytes);
}


int v3d_phoenix_scanout_active(void)
{
	return (A.scan_claims != 0u) ? 1 : 0;
}


int v3d_phoenix_scanout_double(void)
{
	return (A.scan_nbuf >= 2u) ? 1 : 0;
}


int v3d_phoenix_scanout_nbuf(void)
{
	return (A.scan_nbuf != 0u) ? (int)A.scan_nbuf : 1;
}


void v3d_phoenix_set_next_scanout(void)
{
	A.next_scanout = 1;
}


int v3d_phoenix_peek_next_scanout(void)
{
	return A.next_scanout;
}


static void flipstat(void)
{
	uint64_t now = now_us(), dt;
	unsigned long cfps;
	const char *e;

	if (A.flipstat == 0) {
		e = getenv("V3D_FLIPSTAT");
		A.flipstat = ((e != NULL) && (e[0] == '0')) ? 2 : 1;
		e = getenv("V3D_FLIPSTAT_MS");
		A.flipstat_ms = ((e != NULL) && (atoi(e) > 0)) ? (unsigned)atoi(e) : 5000u;
		A.win_t0 = now;
		A.first_t = now;
	}
	A.flips++;
	if (A.flipstat != 1) {
		return;
	}
	A.win_flips++;
	dt = now - A.win_t0;
	if (dt < (uint64_t)A.flipstat_ms * 1000u) {
		return;
	}
	/* Same line format as the in-process winsys, so every existing grader reads
	 * it; the second line is the new lane's client-side wait accounting. */
	cfps = (unsigned long)((A.win_flips * 100000000ull + dt / 2u) / dt);
	fprintf(stderr, "v3d-winsys: flipstat %lu frames in %lu ms = %lu.%02lu fps (total %lu)\n", A.win_flips,
		(unsigned long)(dt / 1000u), cfps / 100u, cfps % 100u, A.flips);
	fprintf(stderr, "v3da-winsys: cstat t=%lums fr=%lu cl=%u tfu=%u csd=%u create=%u wait_us=%llu ipc_waits=%u\n",
		(unsigned long)((now - A.first_t) / 1000u), A.win_flips, A.n_cl, A.n_tfu, A.n_csd, A.n_create,
		(unsigned long long)A.wait_us, A.n_waits_ipc);
	A.win_flips = 0u;
	A.win_t0 = now;
	A.n_cl = 0u;
	A.n_tfu = 0u;
	A.n_csd = 0u;
	A.n_create = 0u;
	A.wait_us = 0u;
	A.n_waits_ipc = 0u;
}


/* Present buffer `buf`: the SERVER pans the firmware display once the last render
 * this client submitted has completed (the glue's glFinish normally made that
 * true already; the fence gate makes it true regardless). */
void v3d_phoenix_flip(int buf)
{
	v3da_fence_t f;
	v3da_flip_resp_t r;
	int rc;

	if ((A.scan_nbuf < 2u) || (conn_get() != 0)) {
		return;
	}
	if (buf < 0) {
		buf = 0;
	}
	if ((uint32_t)buf >= A.scan_nbuf) {
		buf = (int)A.scan_nbuf - 1;
	}
	pthread_mutex_lock(&A.lock);
	f = A.last_render;
	pthread_mutex_unlock(&A.lock);
	rc = v3da_flip(&A.conn, (uint32_t)buf, (f.seqno != 0u) ? &f : NULL, &r);
	if (rc == -EBUSY) {
		(void)fence_wait_timed(&f, FOREVER_NS);   /* server flip queue full: wait here */
		rc = v3da_flip(&A.conn, (uint32_t)buf, NULL, &r);
	}
	A.shown = (uint32_t)buf;
	flipstat();
}


unsigned long v3d_phoenix_flip_count(void)
{
	return A.flips;
}


/* Screenshot helper: read the firmware framebuffer itself (as the winsys does). */
uint32_t v3d_phoenix_scanout_readback(void *dst, int buf, uint32_t bytes)
{
	uint32_t n, off, nbuf = (A.scan_nbuf != 0u) ? A.scan_nbuf : 1u;
	void *m;

	if ((A.scan_pa == 0u) || (A.scan_bytes == 0u) || (dst == NULL)) {
		return 0u;
	}
	if (A.scan_cpu == NULL) {
		m = mmap(NULL, (size_t)nbuf * A.scan_bytes, PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_UNCACHED | MAP_ANONYMOUS | MAP_PHYSMEM, -1, (addr_t)A.scan_pa);
		if (m == MAP_FAILED) {
			return 0u;
		}
		A.scan_cpu = m;
	}
	if (buf < 0) {
		off = A.shown * A.scan_bytes;
	}
	else {
		off = (((uint32_t)buf < nbuf) ? (uint32_t)buf : 0u) * A.scan_bytes;
	}
	n = (bytes < A.scan_bytes) ? bytes : A.scan_bytes;
	memcpy(dst, (const void *)(A.scan_cpu + off), n);
	return n;
}


void v3d_phoenix_harness_reset(void)
{
}


uint32_t v3d_phoenix_last_bin_crc(uint32_t *qma, uint32_t *qms, uint32_t *crc)
{
	if (qma != NULL) {
		*qma = 0u;
	}
	if (qms != NULL) {
		*qms = 0u;
	}
	if (crc != NULL) {
		*crc = 0u;
	}
	return 0u;
}


/* ========================================================================= */
/* Power: owned by the server                                                  */
/* ========================================================================= */

/* A client re-running power-on would toggle the V3D clock/reset under the live
 * server (the destructive two-owner conflict). The server powered the GPU before
 * it registered /dev/v3d-async. */
int v3d_phoenix_powerOn(void)
{
	return (conn_get() == 0) ? 0 : -1;
}


int v3d_phoenix_reset(void)
{
	return 0;   /* resets are the server's watchdog decision */
}


void v3d_phoenix_fb_flip(unsigned yoff)
{
	v3d_phoenix_flip((A.scan_h != 0u) ? (int)(yoff / A.scan_h) : 0);
}


unsigned v3d_phoenix_fb_virtual_height(void)
{
	return A.scan_virt_h;
}


void v3d_phoenix_logColdState(void)
{
}
