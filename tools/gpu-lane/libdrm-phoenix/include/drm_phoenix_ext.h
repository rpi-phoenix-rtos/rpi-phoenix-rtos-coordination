/*
 * Phoenix-RTOS
 *
 * libdrm-phoenix - PROPOSED additions to the rpi4-v3d-async and rpi4-kms wire
 * protocols (M3 server work list)
 *
 * Nothing here changes an existing opcode or struct of v3da_proto.h or
 * kms_proto.h. Each item is either a request layout for an opcode the protocol
 * already reserves (the server answers -ENOSYS today), or a NEW opcode number
 * appended after the last one of its group. libdrm-phoenix speaks each item only
 * when the server's protocol version (HELLO) says it exists; until then the DRM
 * call fails with ENOSYS locally. When a server implements an item, move its
 * definitions into that server's *_proto.h, bump the protocol version there,
 * and delete them here.
 *
 * Copyright 2026 Phoenix Systems
 * Author: Witold Bołt
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _DRM_PHOENIX_EXT_H_
#define _DRM_PHOENIX_EXT_H_

#include <stdint.h>


/* ------------------------------------------------------------------------- */
/* rpi4-v3d-async                                                             */
/* ------------------------------------------------------------------------- */

/*
 * V3DA_OP_BO_IMPORT (21): IMPLEMENTED in M3 part 2 - the request layout
 * (v3da_bo_import_req_t) and its semantics moved to v3da_proto.h, additively
 * inside proto 2 (a part-2 server answers the opcode -ENOSYS, so no version bump
 * was needed and libdrm-phoenix sends it unconditionally). Only the namespace
 * names stay here; their values are V3DA_IMPORT_NS_*.
 */
#ifndef V3DA_HAVE_BO_IMPORT
#error "v3da_proto.h predates BO_IMPORT (M3 part 2): build against the current tools/gpu-lane/v3d-async"
#endif

enum drmphx_ns {
	DRMPHX_NS_KMSBUF = V3DA_IMPORT_NS_KMSBUF,   /* "/kmsbuf/<id>"  (rpi4-kms dumb buffers) */
	DRMPHX_NS_V3DBUF = V3DA_IMPORT_NS_V3DBUF    /* "/v3dbuf/<id>"  (rpi4-v3d-async BOs, V3DA_OP_BO_EXPORT) */
};

/*
 * V3DA_OP_BO_EXPORT (22, NEW, needs V3DA proto >= 3): PRIME export of a BO.
 * M1a BOs are one MAP_CONTIGUOUS block each, so memExport() of the whole block
 * under {v3dbuf port, handle} works as for kms pool buffers. Request: v3da_bo_req_t
 * {handle}; reply: v3da_bo_resp_t with mem.kind = V3DA_MEM_OID, mem.port = the
 * "/v3dbuf" namespace port, mem.addr = id. The namespace follows E1 section 1
 * (mtLookup/atMode/mtOpen 0/refuse atSize) exactly like rpi4-kms's /kmsbuf thread.
 */
#define V3DA_OP_BO_EXPORT_EXT   22u
#define V3DA_PROTO_BO_EXPORT    3u
#define V3DA_BUF_NS_EXT         "/v3dbuf"

/*
 * Syncobj sharing across processes (DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD without
 * EXPORT_SYNC_FILE, and real sync_file fds): NEW ops, needs V3DA proto >= 3.
 * A "/v3dsync/<id>" namespace whose descriptors name a server syncobj (opaque
 * fd) or a frozen fence (sync file); poll() on it = atPollStatus against the
 * fence page. Not specified further here: the DRI3/Wayland milestones (M4/M6)
 * drive it. libdrm-phoenix emulates sync files in-process meanwhile.
 */
#define V3DA_OP_SYNCOBJ_EXPORT_EXT  55u
#define V3DA_OP_SYNCOBJ_FDIMPORT_EXT 56u


/* ------------------------------------------------------------------------- */
/* rpi4-kms                                                                   */
/* ------------------------------------------------------------------------- */

/*
 * KMS_OP_PRIME_IMPORT (38, NEW, needs KMS proto >= 2): import a foreign buffer
 * (e.g. a v3d BO exported through /v3dbuf) as a dumb-BO handle so ADDFB2 can
 * scan it out. Request = v3da_bo_import_req_t (same layout, v3da_proto.h); reply
 * kms_dumb_resp_t. The server maps the buffer, checks contiguity and the
 * < 1 GiB scan-out limit (E6) and refuses what the HVS cannot fetch. Importing
 * one of this client's own /kmsbuf exports returns the original handle
 * (libdrm-phoenix already short-circuits that case locally).
 */
#define KMS_OP_PRIME_IMPORT_EXT 38u
#define KMS_PROTO_PRIME_IMPORT  2u

/*
 * Implicit sync for flips (research 4.5, M2 section 8): V3DA_OP_BO_LAST_FENCE
 * (NEW, 23) - the last-writer fence of an imported BO - so rpi4-kms can gate a
 * PAGE_FLIP of a GPU-rendered buffer that carries no IN_FENCE_FD, for
 * CROSS-PROCESS producers (Xorg, compositors). A single-process GBM app does not
 * need it: libdrm-phoenix attaches the BO's mirrored last-use fence itself (G13,
 * M3 part 2).
 */
#define V3DA_OP_BO_LAST_FENCE_EXT 23u

#endif /* _DRM_PHOENIX_EXT_H_ */
