/*
 * gl_bo_import.c — exercise the Phoenix BO-import path end to end.
 *
 * Step 2's verification, from docs/misc/2026-09-09-gl-window-buffer-sharing-work-order.md.
 * mesa 3b339c93a07 added a WINSYS_HANDLE_TYPE_SHARED case that opens a BO by its
 * v3d-srv daemon handle, but nothing on this port calls resource_from_handle yet,
 * so the code shipped INERT. This proves it in isolation, before the X server is
 * touched -- debugging an untested import inside the demo-critical server is
 * exactly the mistake worth avoiding.
 *
 * What it does, deliberately without Mesa allocating the buffer:
 *   1. create a BO through the RAW daemon RPC (as a different process would)
 *   2. fill it with a deterministic pattern through its MAP_PHYSMEM view
 *   3. import it with pscreen->resource_from_handle(TYPE_SHARED)
 *   4. map the IMPORTED resource through the pipe context and compare
 *
 * Step 4 is the real assertion: it proves the import produced a usable resource
 * addressing the same physical pages, and it exercises the lazy MMAP_BO that
 * v3d_bo_open_handle() defers.
 *
 * Build (daemon-client flavour; the in-process winsys would fight the daemon):
 *   GL_SMOKE_SRC=gl_bo_import.c python3 tools/v3d-driver-port/build-gl-smoke-daemon.py
 * Run with the daemon up: `startx_gpu --quit-after 12 wmaker` leaves it running.
 *
 * Copyright 2026 Phoenix Systems  SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/msg.h>

#include "pipe/p_screen.h"
#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "util/box.h"
#include "frontend/winsys_handle.h"
#include "drm-uapi/drm_fourcc.h"

/* The daemon's RPC ABI. Included by relative path deliberately: the shared
 * transform() in build-v3d-phoenix.py adds -I for the mesa/ port dir, the shim
 * dir and rpi4-vcmbox, but not the rpi4-v3d dir itself -- and widening a helper
 * every v3d build goes through, for one test harness, is the wrong trade.
 * Duplicating the structs here would be worse: they are an ABI shared with the
 * daemon and would silently drift. */
#include "../../sources/phoenix-rtos-devices/gpu/rpi4-v3d/v3d_rpc.h"

#define TEX_W    64
#define TEX_H    64
#define TEX_BPP  4
#define TEX_STRIDE (TEX_W * TEX_BPP)
#define TEX_BYTES  (TEX_STRIDE * TEX_H)
#define BO_BYTES   (64u * 1024u)

struct pipe_screen_config;
struct pipe_screen *v3d_screen_create(int fd, const struct pipe_screen_config *config,
                                      struct renderonly *ro);

static oid_t v3d_oid;

static int rpc(const v3d_rpc_req_t *req, v3d_rpc_resp_t *out)
{
	msg_t msg;
	const v3d_rpc_resp_t *resp = (const v3d_rpc_resp_t *)msg.o.raw;
	int err;

	memset(&msg, 0, sizeof(msg));
	msg.type = mtDevCtl;
	msg.oid = v3d_oid;
	memcpy(msg.i.raw, req, sizeof(*req));

	err = msgSend(v3d_oid.port, &msg);
	if (err < 0) {
		return err;
	}
	if (out != NULL) {
		memcpy(out, resp, sizeof(*out));
	}
	return resp->err;
}

/* Deterministic, position-dependent so a shifted/offset mapping is caught too. */
static unsigned char pat(unsigned i)
{
	return (unsigned char)((i * 7u + 0x3du) & 0xffu);
}

int main(void)
{
	v3d_rpc_req_t req;
	v3d_rpc_resp_t resp;
	struct pipe_screen_config cfg;
	struct pipe_screen *pscreen;
	struct pipe_context *pipe;
	struct pipe_resource tmpl;
	struct winsys_handle wh;
	struct pipe_resource *imported;
	struct pipe_transfer *xfer = NULL;
	struct pipe_box box;
	unsigned char *raw;
	void *map;
	unsigned i, bad;
	uint32_t handle;

	printf("gl_bo_import: import a daemon BO Mesa did not allocate\n");

	if (lookup("/dev/" V3D_RPC_DEV_NAME, NULL, &v3d_oid) < 0) {
		printf("gl_bo_import: FAIL cannot resolve /dev/%s (is rpi4-v3d up?)\n",
			V3D_RPC_DEV_NAME);
		return 1;
	}

	/* (1) allocate OUTSIDE Mesa, via the raw RPC. */
	memset(&req, 0, sizeof(req));
	req.op = V3D_RPC_CREATE_BO;
	req.size = BO_BYTES;
	if (rpc(&req, &resp) != 0) {
		printf("gl_bo_import: FAIL CREATE_BO\n");
		return 1;
	}
	handle = resp.handle;
	printf("gl_bo_import: raw BO handle=%u pa=0x%llx size=%u gpuva=0x%x\n",
		handle, (unsigned long long)resp.pa, resp.size, resp.gpuva);

	/* (2) fill it through its own physical mapping. */
	raw = mmap(NULL, resp.size, PROT_READ | PROT_WRITE,
		MAP_PHYSMEM | MAP_ANONYMOUS | MAP_UNCACHED, -1, (addr_t)resp.pa);
	if (raw == MAP_FAILED) {
		printf("gl_bo_import: FAIL mmap(MAP_PHYSMEM) errno=%d\n", errno);
		return 1;
	}
	for (i = 0; i < TEX_BYTES; i++) {
		raw[i] = pat(i);
	}

	/* (3) bring Mesa up and import the handle. */
	memset(&cfg, 0, sizeof(cfg));
	pscreen = v3d_screen_create(0, &cfg, NULL);
	if (pscreen == NULL) {
		printf("gl_bo_import: FAIL v3d_screen_create\n");
		return 1;
	}
	if (pscreen->resource_from_handle == NULL) {
		printf("gl_bo_import: FAIL resource_from_handle is NULL\n");
		return 1;
	}
	pipe = pscreen->context_create(pscreen, NULL, 0);
	if (pipe == NULL) {
		printf("gl_bo_import: FAIL context_create\n");
		return 1;
	}

	memset(&tmpl, 0, sizeof(tmpl));
	tmpl.target = PIPE_TEXTURE_2D;
	tmpl.format = PIPE_FORMAT_R8G8B8A8_UNORM;
	tmpl.width0 = TEX_W;
	tmpl.height0 = TEX_H;
	tmpl.depth0 = 1;
	tmpl.array_size = 1;
	tmpl.last_level = 0;
	tmpl.usage = PIPE_USAGE_DEFAULT;
	tmpl.bind = PIPE_BIND_SAMPLER_VIEW | PIPE_BIND_RENDER_TARGET;

	memset(&wh, 0, sizeof(wh));
	wh.type = WINSYS_HANDLE_TYPE_SHARED;   /* Phoenix: the daemon's global handle */
	wh.handle = handle;
	wh.stride = TEX_STRIDE;
	wh.offset = 0;
	wh.modifier = DRM_FORMAT_MOD_LINEAR;

	imported = pscreen->resource_from_handle(pscreen, &tmpl, &wh, 0);
	if (imported == NULL) {
		printf("gl_bo_import: FAIL resource_from_handle returned NULL\n");
		return 1;
	}
	printf("gl_bo_import: imported resource %ux%u fmt=%d\n",
		imported->width0, imported->height0, (int)imported->format);

	/* (4) map the IMPORTED resource through Mesa and compare. */
	u_box_2d(0, 0, TEX_W, TEX_H, &box);
	map = pipe->texture_map(pipe, imported, 0, PIPE_MAP_READ, &box, &xfer);
	if (map == NULL) {
		printf("gl_bo_import: FAIL texture_map on the imported resource\n");
		return 1;
	}
	printf("gl_bo_import: mapped stride=%u (raw stride=%u)\n",
		xfer ? xfer->stride : 0u, (unsigned)TEX_STRIDE);

	bad = 0;
	for (i = 0; i < TEX_H; i++) {
		const unsigned char *row = (const unsigned char *)map
			+ (size_t)i * (xfer ? xfer->stride : TEX_STRIDE);
		unsigned j;

		for (j = 0; j < TEX_STRIDE; j++) {
			if (row[j] != pat(i * TEX_STRIDE + j)) {
				bad++;
			}
		}
	}
	pipe->texture_unmap(pipe, xfer);

	printf("gl_bo_import: compare %s (%u/%u bytes wrong)\n",
		(bad == 0) ? "MATCH" : "MISMATCH", bad, (unsigned)TEX_BYTES);

	if (bad != 0) {
		printf("gl_bo_import: VERDICT FAIL — import mapped the wrong memory\n");
		return 1;
	}
	printf("gl_bo_import: VERDICT PASS — Mesa imported a BO it did not allocate"
	       " and sees the same pixels\n");
	return 0;
}
