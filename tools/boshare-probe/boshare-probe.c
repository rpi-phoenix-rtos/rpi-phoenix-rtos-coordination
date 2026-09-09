/*
 * boshare-probe — can two processes share a V3D BO through the v3d-srv daemon?
 *
 * This is step 0 of docs/misc/2026-09-09-gl-window-buffer-sharing-work-order.md.
 * That work order's whole premise is that the daemon's BO handles are GLOBAL
 * rather than per-client, so the X server could name a BO the GL client created
 * and texture from it instead of receiving 1.2 MB of pixels per frame. That was
 * read out of v3d_gpu.c (one `W.bos[]`, `handle == slot + 1`, `pbo_get()` with no
 * client scoping) but never TESTED -- and on this project several confident
 * readings have died on contact with measurement, so it gets tested before
 * anything is built on it.
 *
 * What it proves, or refutes, with no changes to any shipped component:
 *   1. a second process can MMAP_BO a handle it did not create
 *   2. the physical address it gets back is the same one
 *   3. writes made by one process are visible to the other, both directions
 *
 * Copyright 2026 Phoenix Systems
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/msg.h>
#include <sys/threads.h>
#include <sys/wait.h>

#include "v3d_rpc.h"

#define BO_SIZE   (64u * 1024u)
#define PAT_A     0xa5
#define PAT_B     0x5c

static oid_t v3d_oid;

static int resolve(void)
{
	if (lookup("/dev/" V3D_RPC_DEV_NAME, NULL, &v3d_oid) == 0) {
		return 0;
	}
	return -1;
}

static int call(const v3d_rpc_req_t *req, v3d_rpc_resp_t *out)
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

static void *map_pa(uint64_t pa, uint32_t size)
{
	void *p = mmap(NULL, size, PROT_READ | PROT_WRITE,
		MAP_PHYSMEM | MAP_ANONYMOUS | MAP_UNCACHED, -1, (addr_t)pa);
	return (p == MAP_FAILED) ? NULL : p;
}

int main(void)
{
	v3d_rpc_req_t req;
	v3d_rpc_resp_t resp;
	uint32_t handle;
	uint64_t pa_parent;
	uint32_t size;
	uint32_t gpuva_parent;
	void *cpu;
	pid_t pid;
	int st, rc;

	printf("boshare-probe: can a second process map a BO it did not create?\n");

	if (resolve() != 0) {
		printf("boshare-probe: FAIL cannot resolve /dev/%s\n", V3D_RPC_DEV_NAME);
		return 1;
	}

	/* Parent creates the BO and fills it with PAT_A. */
	memset(&req, 0, sizeof(req));
	req.op = V3D_RPC_CREATE_BO;
	req.size = BO_SIZE;
	rc = call(&req, &resp);
	if (rc != 0) {
		printf("boshare-probe: FAIL CREATE_BO rc=%d\n", rc);
		return 1;
	}
	handle = resp.handle;
	pa_parent = resp.pa;
	size = resp.size;
	gpuva_parent = resp.gpuva;
	printf("boshare-probe: parent created handle=%u pa=0x%llx size=%u gpuva=0x%x\n",
		handle, (unsigned long long)pa_parent, size, gpuva_parent);

	cpu = map_pa(pa_parent, size);
	if (cpu == NULL) {
		printf("boshare-probe: FAIL parent mmap(MAP_PHYSMEM) errno=%d\n", errno);
		return 1;
	}
	memset(cpu, PAT_A, BO_SIZE);

	pid = fork();
	if (pid < 0) {
		printf("boshare-probe: FAIL fork errno=%d\n", errno);
		return 1;
	}

	if (pid == 0) {
		/* CHILD: a different process. Ask the daemon to map the PARENT's
		 * handle -- the whole question is whether that is even allowed. */
		void *ccpu;
		unsigned i, bad = 0;
		const unsigned char *q;

		if (resolve() != 0) {
			printf("boshare-probe:   child FAIL resolve\n");
			_exit(2);
		}
		memset(&req, 0, sizeof(req));
		req.op = V3D_RPC_MMAP_BO;
		req.handle = handle;
		rc = call(&req, &resp);
		if (rc != 0) {
			printf("boshare-probe:   child FAIL MMAP_BO(handle=%u) rc=%d"
			       " -- handles are NOT global\n", handle, rc);
			_exit(2);
		}
		printf("boshare-probe:   child MMAP_BO -> pa=0x%llx size=%u (parent pa=0x%llx) %s\n",
			(unsigned long long)resp.pa, resp.size,
			(unsigned long long)pa_parent,
			(resp.pa == pa_parent) ? "SAME" : "DIFFERENT");
		if (resp.pa != pa_parent) {
			_exit(2);
		}

		/* The only daemon-side call Mesa's importer makes:
		 * v3d_bo_open_handle() (v3d_bufmgr.c:339) needs DRM_IOCTL_V3D_GET_BO_OFFSET
		 * to succeed for the handle and asserts the returned offset is non-zero.
		 * If that works for a FOREIGN handle, the import is mechanical. */
		{
			uint32_t bo_size = resp.size;
			v3d_rpc_req_t oreq;
			v3d_rpc_resp_t oresp;

			memset(&oreq, 0, sizeof(oreq));
			oreq.op = V3D_RPC_GET_BO_OFFSET;
			oreq.handle = handle;
			rc = call(&oreq, &oresp);
			printf("boshare-probe:   child GET_BO_OFFSET(handle=%u) rc=%d gpuva=0x%x"
			       " (parent gpuva=0x%x) %s\n", handle, rc, oresp.gpuva,
			       gpuva_parent,
			       (rc == 0 && oresp.gpuva == gpuva_parent && oresp.gpuva != 0)
			               ? "SAME+NONZERO" : "PROBLEM");
			if (rc != 0 || oresp.gpuva != gpuva_parent || oresp.gpuva == 0) {
				_exit(2);
			}
			resp.size = bo_size;
		}

		ccpu = map_pa(resp.pa, resp.size);
		if (ccpu == NULL) {
			printf("boshare-probe:   child FAIL mmap errno=%d\n", errno);
			_exit(2);
		}
		/* (1) does the child see the parent's pattern? */
		q = (const unsigned char *)ccpu;
		for (i = 0; i < BO_SIZE; i++) {
			if (q[i] != PAT_A) { bad++; }
		}
		printf("boshare-probe:   child read parent's pattern: %s (%u/%u bytes wrong)\n",
			(bad == 0) ? "MATCH" : "MISMATCH", bad, BO_SIZE);
		if (bad != 0) {
			_exit(2);
		}
		/* (2) write a different pattern for the parent to see. */
		memset(ccpu, PAT_B, BO_SIZE);
		_exit(0);
	}

	/* PARENT: wait, then check it sees the CHILD's writes. */
	if (waitpid(pid, &st, 0) < 0) {
		printf("boshare-probe: FAIL waitpid errno=%d\n", errno);
		return 1;
	}
	if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
		printf("boshare-probe: VERDICT NOT-SHAREABLE (child status 0x%x)\n", st);
		return 1;
	}
	{
		const unsigned char *q = (const unsigned char *)cpu;
		unsigned i, bad = 0;

		for (i = 0; i < BO_SIZE; i++) {
			if (q[i] != PAT_B) { bad++; }
		}
		printf("boshare-probe: parent read child's pattern: %s (%u/%u bytes wrong)\n",
			(bad == 0) ? "MATCH" : "MISMATCH", bad, BO_SIZE);
		if (bad != 0) {
			printf("boshare-probe: VERDICT NOT-COHERENT\n");
			return 1;
		}
	}

	memset(&req, 0, sizeof(req));
	req.op = V3D_RPC_GEM_CLOSE;
	req.handle = handle;
	(void)call(&req, &resp);

	printf("boshare-probe: VERDICT SHAREABLE — handles are global, pa matches,"
	       " and writes are visible BOTH ways\n");
	return 0;
}
