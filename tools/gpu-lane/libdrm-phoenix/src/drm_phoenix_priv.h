/*
 * Phoenix-RTOS
 *
 * libdrm-phoenix - internal interfaces of the Phoenix backend
 *
 * Copyright 2026 Phoenix Systems
 * Author: Witold Bołt
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _DRM_PHOENIX_PRIV_H_
#define _DRM_PHOENIX_PRIV_H_

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/msg.h>

#include "drm_phoenix_logic.h"
#include "v3da_proto.h"
#include "kms_proto.h"
#include "drm_phoenix_ext.h"


/* The two servers this library talks to. */
enum drmphx_srv {
	DRMPHX_SRV_NONE = 0,
	DRMPHX_SRV_KMS,    /* rpi4-kms: /dev/dri/card0 (fallback /dev/kms) */
	DRMPHX_SRV_V3D     /* rpi4-v3d-async: /dev/dri/renderD128 + /dev/dri/card1 (fallback /dev/v3d-async) */
};

/* Canonical node names (what drmGetDevices2 reports when they resolve) and the
 * names the servers register today (the fallback). */
#define DRMPHX_PATH_CARD0      "/dev/dri/card0"
#define DRMPHX_PATH_CARD1      "/dev/dri/card1"
#define DRMPHX_PATH_RENDER     "/dev/dri/renderD128"
#define DRMPHX_PATH_KMS_LEGACY "/dev/" KMS_DEV_NAME
#define DRMPHX_PATH_V3D_LEGACY "/dev/" V3DA_DEV_NAME



/* ------------------------------------------------------------------------- */
/* Per-connection state (one per server client = one per open() of a node;    */
/* dup()ed descriptors share it)                                              */
/* ------------------------------------------------------------------------- */

#define DRMPHX_V3D_BO_CHUNK   256u
#define DRMPHX_V3D_BO_CHUNKS  32u     /* 8192 slots == the server's 13-bit handle slot */
#define DRMPHX_V3D_MAX_SYNC   256u
#define DRMPHX_V3D_NPARAM     32u
#define DRMPHX_KMS_MAX_DUMB   256u
#define DRMPHX_KMS_MAX_PLANES (KMS_MAX_CRTCS * KMS_PLANES_PER_CRTC)

typedef struct {
	uint32_t handle;          /* 0 = free */
	uint32_t gpuva;
	uint32_t size;
	uint32_t imported;        /* 1 = BO_IMPORT'ed (PRIME) */
	v3da_memref_t mem;        /* kind NONE = not known yet (ask BO_MMAP) */
	v3da_fence_t last;        /* last job of this client that used it (seqno 0 = none) */
} drmphx_v3d_bo_t;

typedef struct {
	uint32_t handle;          /* 0 = free */
	int state;                /* enum v3da_syncobj_state (mirror of the server's) */
	v3da_fence_t fence;
} drmphx_v3d_sync_t;

typedef struct {
	uint32_t handle;          /* 0 = free */
	uint32_t pitch;
	uint64_t size;
	kms_memref_t mem;
} drmphx_kms_dumb_t;

typedef struct drmphx_conn {
	struct drmphx_conn *next;
	int srv;                  /* enum drmphx_srv */
	int node_type;            /* DRM_NODE_PRIMARY / DRM_NODE_RENDER */
	oid_t oid;                /* {server port, client id}: direct requests */
	int refs;                 /* descriptor-table entries (global lock) */
	int users;                /* calls in progress (global lock) */
	int dead;                 /* no longer reachable from the descriptor table */

	pthread_mutex_t lock;     /* the tables below */
	pthread_mutex_t xfer;     /* the scratch buffer */
	void *scratch;            /* page-aligned bounce for i.data / o.data (E5) */
	size_t scratch_size;

	union {
		struct {
			v3da_hello_t hello;
			const volatile v3da_fence_page_t *fp;   /* read-only, cached */
			uint64_t param[DRMPHX_V3D_NPARAM];
			uint8_t have_param[DRMPHX_V3D_NPARAM];
			drmphx_v3d_bo_t *bo[DRMPHX_V3D_BO_CHUNKS];
			drmphx_v3d_sync_t sync[DRMPHX_V3D_MAX_SYNC];
			uint32_t ipc_waits;
		} v3d;
		struct {
			kms_hello_t hello;
			uint32_t buf_port;
			drmphx_kms_dumb_t dumb[DRMPHX_KMS_MAX_DUMB];
			uint8_t plane_valid[DRMPHX_KMS_MAX_PLANES];
			kms_atomic_plane_t plane[DRMPHX_KMS_MAX_PLANES];   /* last committed state (mirror) */
		} kms;
	} u;
} drmphx_conn_t;


/* ------------------------------------------------------------------------- */
/* Core (xf86drm_phoenix.c)                                                   */
/* ------------------------------------------------------------------------- */

/* libdrm convention: return -1 with errno. */
int drmphx_fail(int negerr);

uint64_t drmphx_now_us(void);
int64_t drmphx_now_ns(void);

/* One raw request (req/resp are the 64-byte v3da_req_t/kms_req_t envelopes, the
 * magic already set). idata/odata are bounced through the connection's
 * page-aligned scratch buffer; i.size = iwire (>= isize: pass a page multiple to
 * keep the end aligned when the server parses by counts), o.size = osize rounded
 * up to a page. Returns the IPC error, the server's msg.o.err or the per-op err. */
int drmphx_call(drmphx_conn_t *c, const void *req, void *resp, const void *idata, size_t isize, size_t iwire,
	void *odata, size_t osize);

/* Resolve a descriptor that is not a DRM node but a buffer name (PRIME fd):
 * "/kmsbuf/<id>" today. Fills *m (kind OID, port, id); size stays 0 unless this
 * process exported it. Returns 0 or a negative errno. */
int drmphx_prime_fd_lookup(int fd, kms_memref_t *m);

/* Record/forget a PRIME fd this process created (so its size and origin are
 * known to a later import in this process). */
void drmphx_prime_fd_note(int fd, const kms_memref_t *m, int srv, uint32_t handle);

/* Sync-file emulation: an exported "sync file" is a dup() of the render node
 * descriptor plus a fence snapshot (process-local; cross-process sync files are
 * a server gap). */
int drmphx_syncfile_new(int dev_fd, const v3da_fence_t *f);
int drmphx_syncfile_get(int fd, v3da_fence_t *f);

/* Map a memref (PHYS: MAP_PHYSMEM; OID: open(<ns>/<id>) + mmap). */
void *drmphx_map_memref(uint16_t kind, uint16_t cache, uint32_t port, uint64_t size, uint64_t addr, size_t len,
	int prot, void *hint, int fixed);


/* ------------------------------------------------------------------------- */
/* Per-server marshalling                                                     */
/* ------------------------------------------------------------------------- */

int drmphx_kms_ioctl(drmphx_conn_t *c, int fd, unsigned nr, void *arg);
int drmphx_kms_hello(drmphx_conn_t *c, int fd);
int drmphx_kms_token_memref(drmphx_conn_t *c, uint32_t handle, kms_memref_t *m);

int drmphx_v3d_ioctl(drmphx_conn_t *c, int fd, unsigned nr, void *arg);
int drmphx_v3d_hello(drmphx_conn_t *c, int fd);
void drmphx_v3d_release(drmphx_conn_t *c);
int drmphx_v3d_stale(const drmphx_conn_t *c);
int drmphx_v3d_token_memref(drmphx_conn_t *c, uint32_t handle, v3da_memref_t *m);

#endif /* _DRM_PHOENIX_PRIV_H_ */
