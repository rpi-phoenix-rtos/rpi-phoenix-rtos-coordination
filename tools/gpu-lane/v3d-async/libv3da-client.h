/*
 * Phoenix-RTOS
 *
 * Raspberry Pi 4 (BCM2711) V3D 4.2 asynchronous render server - client library
 *
 * Thin veneer over /dev/v3d-async (protocol: v3da_proto.h). In M3 this becomes the
 * v3d half of libdrm-phoenix; in M1 part 2 it gains the phoenix_v3d_ioctl() entry
 * point Mesa calls and the transitional present family the SDL2 glue uses.
 *
 * Every function returns 0 or a negative errno unless stated otherwise.
 *
 * Copyright 2026 Phoenix Systems
 * Author: Witold Bołt
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _LIBV3DA_CLIENT_H_
#define _LIBV3DA_CLIENT_H_

#include <stdint.h>
#include <sys/types.h>
#include <sys/msg.h>

#include "v3da_proto.h"


typedef struct {
	int fd;                                  /* keeps the client alive: its close is our death notice */
	oid_t oid;                               /* {server port, client id} for direct requests */
	v3da_hello_t hello;
	const volatile v3da_fence_page_t *fp;    /* read-only, cached */
	int wait_poll;                           /* V3DA_WAIT=poll: never park in the server */
	uint32_t ipc_waits;                      /* slow-path waits issued (statistics) */
} v3da_conn_t;


int v3da_connect(v3da_conn_t *c);
void v3da_disconnect(v3da_conn_t *c);

/* One raw request; resp may be NULL. Returns the per-op err (or the IPC error). */
int v3da_call(v3da_conn_t *c, v3da_req_t *req, v3da_resp_t *resp);

/* Map / unmap a memref (PHYS today; OID after E1). NULL on failure. */
void *v3da_map(const v3da_memref_t *m, int writable);
void v3da_unmap(void *p, const v3da_memref_t *m);

int v3da_get_info(v3da_conn_t *c, v3da_info_t *out);
int v3da_get_param(v3da_conn_t *c, uint32_t param, uint64_t *value);

int v3da_bo_create(v3da_conn_t *c, uint32_t size, uint32_t flags, v3da_bo_create_resp_t *out);
int v3da_bo_close(v3da_conn_t *c, uint32_t handle);
int v3da_bo_mmap(v3da_conn_t *c, uint32_t handle, v3da_bo_resp_t *out);
int v3da_bo_offset(v3da_conn_t *c, uint32_t handle, uint32_t *gpuva);
int v3da_bo_wait(v3da_conn_t *c, uint32_t handle, int64_t timeout_ns);

int v3da_submit_nop(v3da_conn_t *c, uint32_t delay_us, v3da_fence_t *out);

/* Fast path: one acquire load of the fence page. 1 = signalled, 0 = not. */
int v3da_fence_signaled(const v3da_conn_t *c, const v3da_fence_t *f);
/* Wait: fast path, a short spin, then bounded server waits (each <= V3DA_WAIT_MAX_MS)
 * until timeout_ns (< 0 = forever). -ETIMEDOUT on expiry. */
int v3da_fence_wait(v3da_conn_t *c, const v3da_fence_t *f, int64_t timeout_ns);
/* One server wait with an explicit server-side timeout (for tests). */
int v3da_fence_wait_once(v3da_conn_t *c, const v3da_fence_t *f, uint32_t timeout_ms, v3da_fence_wait_resp_t *out);

int v3da_syncobj_create(v3da_conn_t *c, uint32_t flags, uint32_t *handle);
int v3da_syncobj_destroy(v3da_conn_t *c, uint32_t handle);
int v3da_syncobj_reset(v3da_conn_t *c, uint32_t handle);
int v3da_syncobj_signal(v3da_conn_t *c, uint32_t handle);
int v3da_syncobj_query(v3da_conn_t *c, uint32_t handle, v3da_syncobj_resp_t *out);
int v3da_syncobj_wait(v3da_conn_t *c, const uint32_t *handles, uint32_t n, uint32_t flags,
	int64_t timeout_ns, uint32_t *first);

/* Debug / test operations. */
int v3da_dbg_irq_mode(v3da_conn_t *c, uint32_t on, v3da_irq_mode_resp_t *out);
int v3da_dbg_irq_selftest(v3da_conn_t *c, v3da_irq_selftest_resp_t *out);
int v3da_dbg_bo_checksum(v3da_conn_t *c, uint32_t handle, uint32_t off, uint32_t len, v3da_bo_checksum_resp_t *out);
int v3da_dbg_stats(v3da_conn_t *c, v3da_stats_t *out);
int v3da_dbg_quit(v3da_conn_t *c, v3da_quit_resp_t *out);

#endif /* _LIBV3DA_CLIENT_H_ */
