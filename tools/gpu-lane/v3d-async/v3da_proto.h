/*
 * Phoenix-RTOS
 *
 * Raspberry Pi 4 (BCM2711) V3D 4.2 asynchronous render server - wire protocol
 *
 * The contract between rpi4-v3d-async (/dev/v3d-async) and libv3da-client. Design:
 * docs/gpu-new-lane/M1-async-render-server.md (section 10).
 *
 * Transport
 *   - A client open()s /dev/v3d-async. The server answers mtOpen with a positive
 *     per-open CLIENT ID, which the kernel stores as the descriptor's oid.id; the
 *     kernel's mtClose for that id is the client-death notification.
 *   - HELLO is an ioctl() on that descriptor (so the server sees the id); every
 *     later request is a direct msgSend(mtDevCtl) to {server port, client id}.
 *   - Requests: v3da_req_t in msg.i.raw, starting with V3DA_MAGIC. An ioctl()-packed
 *     message starts with ioctl_in_t.request, whose bits 31:30 hold IOC_INOUT; the
 *     magic has bit 31 clear, so the two can never be confused.
 *   - Responses: v3da_resp_t in msg.o.raw; msg.o.err is the IPC status (EOK when
 *     the request was handled), v3da_resp_t.err the per-op status.
 *   - Everything fits the 64-byte raw areas except SUBMIT_*, which carry a flat
 *     buffer in msg.i.data (see v3da_submit_t). Waits are raw-only on purpose: a
 *     parked request must hold no payload window in the server (E5).
 *
 * Memory
 *   Buffers are never copied. The server hands out MEMREFS: today a physical
 *   address the client maps with mmap(MAP_PHYSMEM) (V3DA_MEM_PHYS); after the
 *   kernel export (E1) an oid the client opens and maps (V3DA_MEM_OID). The
 *   opcodes do not change when the kind does.
 *
 * Fences
 *   A fence is (slot, queue, seqno). `slot` is the submitting client's row in the
 *   shared read-only fence page; within a slot each queue completes in seqno
 *   order, so a fence is known at submit time and checked with one load.
 *
 * Copyright 2026 Phoenix Systems
 * Author: Witold Bołt
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _V3DA_PROTO_H_
#define _V3DA_PROTO_H_

#include <stdint.h>


#define V3DA_DEV_NAME      "v3d-async"   /* old lane: "v3d-srv" (gpu/rpi4-v3d/v3d_rpc.h) */
#define V3DA_PROTO_VERSION 2u   /* 2: M1 part 2 (submits, scanout/flip, modes) */
#define V3DA_MAGIC         0x41443356u   /* "V3DA" little-endian; bit 31 clear */
#define V3DA_FENCE_MAGIC   0x46443356u   /* "V3DF" */

/* Longest a request may stay parked in the server. The library loops for longer
 * (or infinite) waits, so no client thread is ever parked beyond this: a parked
 * Phoenix client cannot be interrupted or killed (E5 condition 1). */
#define V3DA_WAIT_MAX_MS 2000u


/* ------------------------------------------------------------------------- */
/* Queues                                                                     */
/* ------------------------------------------------------------------------- */

enum v3da_queue {
	V3DA_Q_BIN = 0,      /* CT0, done = FLDONE */
	V3DA_Q_RENDER,       /* CT1, done = FRDONE; waits for its bin job */
	V3DA_Q_TFU,          /* hub TFU, done = TFUC */
	V3DA_Q_CSD,          /* compute dispatch, done = CSDDONE */
	V3DA_Q_CACHE_CLEAN,  /* TMUWCF + L2T clean */
	V3DA_Q_CPU,          /* server-side CPU jobs (v3dv queries; the NOP test job) */
	V3DA_Q_COUNT
};


/* ------------------------------------------------------------------------- */
/* Memory references                                                          */
/* ------------------------------------------------------------------------- */

enum v3da_mem_kind {
	V3DA_MEM_NONE = 0,
	V3DA_MEM_PHYS = 1,   /* addr = physical address; map with MAP_PHYSMEM */
	V3DA_MEM_OID = 2     /* {port, addr = id}: open + mmap(fd) (after E1) */
};

enum v3da_mem_cache {
	V3DA_CACHE_CACHED = 0,
	V3DA_CACHE_UNCACHED = 1   /* map with MAP_UNCACHED; every mapping of one buffer must agree */
};

typedef struct {
	uint16_t kind;    /* enum v3da_mem_kind */
	uint16_t cache;   /* enum v3da_mem_cache */
	uint32_t port;    /* V3DA_MEM_OID only */
	uint64_t size;    /* bytes, page multiple */
	uint64_t addr;    /* PHYS: physical address; OID: object id */
} v3da_memref_t;


/* ------------------------------------------------------------------------- */
/* Fences and the fence page                                                  */
/* ------------------------------------------------------------------------- */

typedef struct {
	uint16_t slot;    /* fence-page row of the submitting client */
	uint16_t queue;   /* enum v3da_queue */
	uint32_t gen;     /* low 32 bits of the slot generation at submit time */
	uint64_t seqno;   /* signalled when slot.completed[queue] >= seqno */
} v3da_fence_t;

#define V3DA_FENCE_PAGE_SIZE  4096u
#define V3DA_FENCE_HDR_SIZE   256u
#define V3DA_FENCE_SLOT_SIZE  64u
#define V3DA_FENCE_NSLOTS     ((V3DA_FENCE_PAGE_SIZE - V3DA_FENCE_HDR_SIZE) / V3DA_FENCE_SLOT_SIZE)   /* 60 */

/* Fence page flag bits (v3da_fence_hdr_t.flags). */
#define V3DA_FP_IRQ    (1u << 0)   /* completions are interrupt-driven (else polled) */
#define V3DA_FP_STORM  (1u << 1)   /* the IRQ storm guard fired; server fell back to polling */
#define V3DA_FP_EXITED (1u << 2)   /* the server has shut down */

/* Every u64 is written with a release store and read with an acquire load
 * (single-copy atomic on AArch64); no seqlock is needed. */
typedef struct {
	uint32_t magic;       /* V3DA_FENCE_MAGIC */
	uint32_t version;     /* V3DA_PROTO_VERSION */
	uint32_t nslots;      /* V3DA_FENCE_NSLOTS */
	uint32_t slot_size;   /* V3DA_FENCE_SLOT_SIZE */
	uint32_t reset_gen;   /* bumped by every GPU reset */
	uint32_t flags;       /* V3DA_FP_* */
	uint32_t server_pid;
	uint32_t pad0;
	uint64_t heartbeat;   /* bumped by every event-thread loop: a frozen value = dead server */
	uint64_t hw_submitted[8];   /* per queue, all clients: jobs kicked to hardware */
	uint64_t hw_completed[8];   /* per queue, all clients: jobs completed (incl. errored) */
	uint64_t rsvd[11];
} v3da_fence_hdr_t;

typedef struct {
	uint64_t completed[V3DA_Q_COUNT];   /* highest completed seqno per queue */
	uint64_t error_seq;                 /* (queue << 56) | seqno of the last job that completed with an error */
	uint64_t gen;                       /* slot generation: changes when the slot is reassigned */
} v3da_fence_slot_t;

typedef struct {
	v3da_fence_hdr_t hdr;
	v3da_fence_slot_t slot[V3DA_FENCE_NSLOTS];
} v3da_fence_page_t;


/* ------------------------------------------------------------------------- */
/* Opcodes                                                                    */
/* ------------------------------------------------------------------------- */

enum v3da_op {
	V3DA_OP_NONE = 0,
	V3DA_OP_HELLO,            /* ioctl only (V3DA_IOC_HELLO) */
	V3DA_OP_GET_INFO,
	V3DA_OP_GET_PARAM,        /* DRM_IOCTL_V3D_GET_PARAM */

	V3DA_OP_BO_CREATE = 16,   /* DRM_IOCTL_V3D_CREATE_BO   (old: V3D_RPC_CREATE_BO) */
	V3DA_OP_BO_CLOSE,         /* DRM_IOCTL_GEM_CLOSE       (old: V3D_RPC_GEM_CLOSE) */
	V3DA_OP_BO_MMAP,          /* DRM_IOCTL_V3D_MMAP_BO     (old: V3D_RPC_MMAP_BO, bare pa) */
	V3DA_OP_BO_GET_OFFSET,    /* DRM_IOCTL_V3D_GET_BO_OFFSET (old: V3D_RPC_GET_BO_OFFSET) */
	V3DA_OP_BO_WAIT,          /* DRM_IOCTL_V3D_WAIT_BO     (old: client-local no-op) */
	V3DA_OP_BO_IMPORT,        /* GEM_OPEN / PRIME import   (reserved) */

	V3DA_OP_SUBMIT_CL = 32,   /* DRM_IOCTL_V3D_SUBMIT_CL   (old: V3D_RPC_SUBMIT_CL, synchronous) */
	V3DA_OP_SUBMIT_TFU,       /* DRM_IOCTL_V3D_SUBMIT_TFU */
	V3DA_OP_SUBMIT_CSD,       /* DRM_IOCTL_V3D_SUBMIT_CSD */
	V3DA_OP_SUBMIT_CPU,       /* DRM_IOCTL_V3D_SUBMIT_CPU  (reserved) */
	V3DA_OP_SUBMIT_NOP,       /* test job on the CPU queue: completes after delay_us */
	V3DA_OP_FENCE_WAIT,       /* slow half of the library's fence wait */

	V3DA_OP_SYNCOBJ_CREATE = 48,   /* DRM_IOCTL_SYNCOBJ_CREATE */
	V3DA_OP_SYNCOBJ_DESTROY,
	V3DA_OP_SYNCOBJ_WAIT,
	V3DA_OP_SYNCOBJ_RESET,
	V3DA_OP_SYNCOBJ_SIGNAL,
	V3DA_OP_SYNCOBJ_QUERY,
	V3DA_OP_SYNCOBJ_IMPORT,        /* attach a fence (sync-file emulation: drmSyncobjImportSyncFile) */

	V3DA_OP_PERFMON_CREATE = 64,   /* reserved: -ENOSYS */
	V3DA_OP_PERFMON_DESTROY,
	V3DA_OP_PERFMON_GET_VALUES,
	V3DA_OP_PERFMON_GET_COUNTER,

	V3DA_OP_SCANOUT_INFO = 80,     /* transitional present family (retired by M2 KMS): fb geometry */
	V3DA_OP_SCANOUT_BO,            /* reserved (scanout BOs are BO_CREATE with V3DA_BO_SCANOUT) */
	V3DA_OP_FLIP,                  /* firmware pan to buffer k, optionally after a fence */

	V3DA_OP_DBG_IRQ_MODE = 112,    /* switch interrupt-driven completion on/off at runtime */
	V3DA_OP_DBG_IRQ_SELFTEST,      /* raise core FLDONE + hub TFUC via INT_SET, report what arrived */
	V3DA_OP_DBG_BO_CHECKSUM,       /* server-side checksum of a BO range (sharing proof) */
	V3DA_OP_DBG_STATS,
	V3DA_OP_DBG_QUIT,
	V3DA_OP_DBG_SET_MODE,          /* serialized / pipelined scheduling + cache-maintenance knobs */
	V3DA_OP_DBG_QSTATS             /* per-queue busy / overlap counters */
};


/* ------------------------------------------------------------------------- */
/* Per-op arguments (inside v3da_req_t.u / v3da_resp_t.u)                     */
/* ------------------------------------------------------------------------- */

/* HELLO travels as ioctl(fd, V3DA_IOC_HELLO, &hello): in = proto, out = the rest. */
typedef struct {
	uint32_t proto;         /* in: V3DA_PROTO_VERSION; out: server's */
	uint32_t client_id;     /* out: == the descriptor's oid.id */
	uint32_t slot;          /* out: fence-page row */
	uint32_t slot_gen;      /* out: low 32 bits of the slot generation */
	uint32_t server_pid;    /* out */
	uint32_t flags;         /* out: V3DA_FP_* snapshot */
	v3da_memref_t fence_page;   /* out: map PROT_READ, cached */
} v3da_hello_t;

/* 'V' group, nr 1. Built by hand (== _IOWR('V', 1, v3da_hello_t)) so this header
 * needs no <sys/ioctl.h>: IOC_INOUT | (len << 16) | ('V' << 8) | 1. */
#define V3DA_IOC_HELLO ((unsigned long)(0xc0000000UL | ((unsigned long)(sizeof(v3da_hello_t) & 0x1fffu) << 16) | \
	((unsigned long)'V' << 8) | 1UL))

typedef struct {
	uint32_t ident[7];      /* CORE0_IDENT0/1/2, HUB_UIFCFG, HUB_IDENT1/2/3 (live registers) */
	uint32_t irq_mode;      /* 1 = interrupt-driven */
	uint32_t irq_num;
	uint32_t clk_rate_hz;   /* firmware GET_CLOCK_RATE(V3D), 0 = unknown */
	uint32_t clk_meas_hz;   /* firmware GET_CLOCK_MEASURED(V3D), 0 = unknown */
	uint32_t max_bos;
	uint32_t nclients;
	uint32_t pad;
} v3da_info_t;

typedef struct {
	uint32_t param;         /* DRM_V3D_PARAM_* */
	uint32_t pad;
} v3da_get_param_req_t;

typedef struct {
	uint64_t value;
} v3da_get_param_resp_t;

#define V3DA_BO_CACHEABLE (1u << 0)   /* == DRM V3D_CREATE_BO_CACHEABLE-style: map cached */
#define V3DA_BO_SCANOUT   (1u << 1)   /* == DRM V3D_CREATE_BO flag bit 1: GPU pages = firmware-fb buffer */

typedef struct {
	uint32_t size;
	uint32_t flags;         /* V3DA_BO_* */
} v3da_bo_create_req_t;

typedef struct {
	uint32_t handle;        /* global, nonzero, never reused while the server lives (generation in the high bits) */
	uint32_t gpuva;         /* == the DRM offset */
	uint32_t size;          /* page-rounded */
	uint32_t scanout;       /* 0, or 1 + the firmware-fb buffer index backing this BO's GPU pages */
	v3da_memref_t mem;      /* the CPU view (for a scanout BO: its own DRAM, which the GPU does NOT use) */
} v3da_bo_create_resp_t;

typedef struct {
	uint32_t handle;
	uint32_t timeout_ms;    /* BO_WAIT only; clamped to V3DA_WAIT_MAX_MS */
} v3da_bo_req_t;

typedef struct {
	uint32_t gpuva;
	uint32_t size;
	v3da_memref_t mem;      /* BO_MMAP */
} v3da_bo_resp_t;

typedef struct {
	uint32_t delay_us;      /* the job "runs" this long on the CPU queue */
	uint32_t pad;
} v3da_nop_req_t;

typedef struct {
	v3da_fence_t fence;
} v3da_fence_resp_t;

typedef struct {
	v3da_fence_t fence;
	uint32_t timeout_ms;    /* clamped to V3DA_WAIT_MAX_MS; 0 = poll once */
	uint32_t pad;
} v3da_fence_wait_req_t;

typedef struct {
	uint64_t completed;     /* slot.completed[queue] at answer time */
	uint32_t error;         /* nonzero if the fence completed with an error */
	uint32_t echo;          /* low 32 bits of the awaited seqno: proves the answer is ours */
} v3da_fence_wait_resp_t;

#define V3DA_SYNCOBJ_CREATE_SIGNALED (1u << 0)
#define V3DA_SYNCOBJ_WAIT_ALL        (1u << 0)
#define V3DA_SYNCOBJ_WAIT_FOR_SUBMIT (1u << 1)
#define V3DA_SYNCOBJ_WAIT_MAX        8u

typedef struct {
	uint32_t handle;        /* DESTROY/RESET/SIGNAL/QUERY */
	uint32_t flags;         /* CREATE: V3DA_SYNCOBJ_CREATE_*; WAIT: V3DA_SYNCOBJ_WAIT_* */
	uint32_t count;         /* WAIT */
	uint32_t timeout_ms;    /* WAIT; clamped to V3DA_WAIT_MAX_MS */
	uint32_t handles[V3DA_SYNCOBJ_WAIT_MAX];   /* WAIT */
} v3da_syncobj_req_t;

enum v3da_syncobj_state {
	V3DA_SYNC_EMPTY = 0,    /* no fence attached */
	V3DA_SYNC_FENCE,        /* fence attached (maybe signalled) */
	V3DA_SYNC_SIGNALED
};

typedef struct {
	uint32_t handle;        /* CREATE */
	uint32_t state;         /* QUERY: enum v3da_syncobj_state */
	uint32_t first;         /* WAIT (any): index of the first signalled handle */
	uint32_t pad;
	v3da_fence_t fence;     /* QUERY */
} v3da_syncobj_resp_t;

typedef struct {
	uint32_t on;            /* DBG_IRQ_MODE: 1 = interrupt-driven, 0 = poll */
	uint32_t pad;
} v3da_irq_mode_req_t;

typedef struct {
	int32_t rc;             /* interrupt() result when switching on */
	uint32_t mode;          /* resulting mode */
	uint32_t irq;
	uint32_t pad;
} v3da_irq_mode_resp_t;

typedef struct {
	uint32_t mode;              /* 1 = irq, 0 = poll */
	uint32_t core_sts_seen;     /* FLDONE visible in raw CTL_INT_STS right after INT_SET (poll mode) */
	uint32_t hub_sts_seen;      /* TFUC visible in raw HUB_INT_STS right after INT_SET (poll mode) */
	uint32_t handler_delta;     /* IRQ handler invocations during the test */
	uint32_t core_events;       /* the event thread saw the stray FLDONE */
	uint32_t hub_events;        /* the event thread saw the stray TFUC */
	uint32_t storm;             /* the storm guard has fired */
	uint32_t core_msk;          /* CTL_INT_MSK_STS */
	uint32_t hub_msk;           /* HUB_INT_MSK_STS */
	uint32_t pad;
} v3da_irq_selftest_resp_t;

typedef struct {
	uint32_t handle;
	uint32_t offset;
	uint32_t len;
	uint32_t pad;
} v3da_bo_checksum_req_t;

typedef struct {
	uint32_t sum;           /* FNV-1a 32 over [offset, offset+len) as the SERVER reads it */
	uint32_t first_word;
} v3da_bo_checksum_resp_t;

/* SYNCOBJ_IMPORT: attach `fence` to syncobj `handle` (state FENCE). */
typedef struct {
	uint32_t handle;
	uint32_t pad;
	v3da_fence_t fence;
} v3da_syncobj_import_req_t;

/* SCANOUT_INFO: the firmware framebuffer the SDL glue found through /dev/fb0.
 * The server asks the firmware for the granted virtual height (GET_VIRTUAL_WH,
 * through /dev/vcmbox) and derives how many stacked page-flip buffers exist. */
typedef struct {
	uint64_t pa;
	uint32_t width;
	uint32_t height;
	uint32_t pitch;
	uint32_t pad;
} v3da_scanout_req_t;

typedef struct {
	uint32_t nbuf;          /* 1 (single, render in place), 2 or 3 (page flip) */
	uint32_t virt_h;        /* firmware-granted virtual height (0 = query failed) */
	uint32_t bytes;         /* one buffer: pitch * height */
	uint32_t claimed;       /* bitmask of buffers backing a live scanout BO */
} v3da_scanout_resp_t;

#define V3DA_FLIP_AFTER_FENCE (1u << 0)   /* pan only once `fence` has signalled */

typedef struct {
	uint32_t buf;           /* 0..nbuf-1 */
	uint32_t flags;         /* V3DA_FLIP_* */
	v3da_fence_t fence;
} v3da_flip_req_t;

typedef struct {
	uint32_t deferred;      /* 1 = queued behind its fence, 0 = panned before the reply */
	uint32_t pending;       /* flips still queued */
	uint32_t flips;         /* pans issued so far */
	uint32_t pad;
} v3da_flip_resp_t;

/* Scheduling modes (DBG_SET_MODE, `-m`). SERIAL is the bring-up default: one job
 * in flight across ALL hardware queues - the old synchronous lane's ordering with
 * asynchronous completion. PIPELINE runs every queue concurrently (one job each),
 * so bin(N+1) overlaps render(N). */
enum v3da_mode { V3DA_MODE_SERIAL = 0, V3DA_MODE_PIPELINE = 1 };

/* Cache-maintenance knobs. Every bit is a DROP of a step of the proven sequence
 * (design section 5 / E2 steps); the default 0 is the old lane's sequence. For
 * E10-style A/B only. */
#define V3DA_KNOB_TLB_ON_CHANGE   (1u << 0)   /* E2 step 5: flush the MMU TLB only when a PTE changed */
#define V3DA_KNOB_NO_L2T_WAIT_NEW (1u << 1)   /* E2 step 6: do not wait for the pre-bin L2T flush */
#define V3DA_KNOB_NO_FIXA         (1u << 2)   /* E2 step 7: drop fix-A (regressed on 2026-07-26) */
#define V3DA_KNOB_NO_HANDOFF_WAIT (1u << 3)   /* E2 step 11: do not wait for the bin->render L2T flush */
#define V3DA_KNOB_CL_CACHE_CLEAN  (1u << 4)   /* E2 step 16: honour SUBMIT_CL_FLUSH_CACHE (TMUWCF + clean) */
/* E2b / H7 (docs/gpu-new-lane/E2b-v3d-render-slowness.md): make the per-job L2T maintenance
 * match Linux v3d_bin_job_run / v3d_render_job_run (v3d_sched.c:238,292 -> v3d_gem.c:250). */
#define V3DA_KNOB_NO_POST_CLEAN   (1u << 5)   /* E2 step 15: no L2T CLEAN after FRDONE (Linux: none after CL) */
#define V3DA_KNOB_NO_HANDOFF_FLUSH (1u << 6)  /* diagnostic: no L2T flush at all before CT1 (Linux HAS one) */
#define V3DA_KNOB_LINUX_ORDER     (1u << 7)   /* pre-bin: L2T flush THEN slice invalidate, as Linux */
#define V3DA_KNOB_PX_LOG          (1u << 8)   /* diagnostic, no GPU effect: log a 16x16 grid hash of every pan */
/* The Linux per-job sequence: TLB on PTE change, one unwaited L2T FLUSH + slices before CT0
 * and before CT1, no fix-A, no post-render clean. */
#define V3DA_KNOB_LINUX           (V3DA_KNOB_TLB_ON_CHANGE | V3DA_KNOB_NO_L2T_WAIT_NEW | V3DA_KNOB_NO_FIXA | \
                                   V3DA_KNOB_NO_HANDOFF_WAIT | V3DA_KNOB_NO_POST_CLEAN | V3DA_KNOB_LINUX_ORDER)
#define V3DA_KNOB_ALL             0x1ffu

#define V3DA_SET_MODE  (1u << 0)
#define V3DA_SET_KNOBS (1u << 1)

typedef struct {
	uint32_t set;           /* V3DA_SET_*: which fields to apply (0 = query) */
	uint32_t mode;          /* enum v3da_mode */
	uint32_t knobs;         /* V3DA_KNOB_* */
	uint32_t pad;
} v3da_mode_req_t;

typedef struct {
	uint32_t mode;
	uint32_t knobs;
	int32_t rc;             /* -EBUSY: jobs in flight, mode not changed */
	uint32_t pad;
} v3da_mode_resp_t;

#define V3DA_QSTATS_GLOBAL 0xffu

typedef struct {
	uint32_t which;         /* a queue (enum v3da_queue) or V3DA_QSTATS_GLOBAL */
	uint32_t reset;         /* 1 = zero the counters after reading */
} v3da_qstats_req_t;

/* Per queue: every figure since the last reset. */
typedef struct {
	uint32_t jobs;          /* completed */
	uint32_t errors;        /* completed with an error (wedge, TFUF, failed dependency) */
	uint64_t busy_us;       /* sum of kick -> done */
	uint64_t wait_us;       /* sum of submit -> kick (queueing + dependencies) */
	uint32_t max_us;        /* longest kick -> done */
	uint32_t oom;           /* BIN: overflow chunks handed out */
	uint32_t pending;       /* queued now (not yet kicked) */
	uint32_t active;        /* 1 = a job is on the hardware now */
} v3da_qstats_q_t;

/* Global: hardware-queue concurrency. */
typedef struct {
	uint64_t window_us;     /* time since the last reset */
	uint64_t any_busy_us;   /* >= 1 hardware queue busy */
	uint64_t overlap_us;    /* >= 2 hardware queues busy (bin || render etc.) */
	uint32_t mode;
	uint32_t knobs;
	uint32_t wedges;        /* watchdog verdicts */
	uint32_t resets;        /* GPU resets */
	uint32_t ovf_free;      /* overflow chunks free now */
	uint32_t ovf_total;
	uint32_t ovf_starved;   /* OUTOMEM with nothing staged / pool empty */
	uint32_t flips;
} v3da_qstats_g_t;

typedef struct {
	uint32_t clients;
	uint32_t bos_live;
	uint32_t bos_quarantined;
	uint32_t bos_pooled;        /* blocks held for reuse (never returned to the kernel) */
	uint32_t bo_quarantine_passed;
	uint32_t pages_to_kernel;   /* expected 0: see the design, section 9.2 */
	uint32_t parked;            /* requests parked right now */
	uint32_t parked_max;
	uint32_t irq_count;
	uint32_t tlb_flushes;
	uint32_t loops;             /* event-thread loops */
	uint32_t timeouts;          /* parked waits answered -ETIMEDOUT */
	uint32_t nops_done;
	uint32_t resets;
} v3da_stats_t;

typedef struct {
	uint32_t parked;        /* parked requests answered on the way out */
	uint32_t inflight;      /* jobs still in flight (quit refused if nonzero) */
} v3da_quit_resp_t;


/* ------------------------------------------------------------------------- */
/* Submit marshaling                                                          */
/* ------------------------------------------------------------------------- */

/* The job descriptors are this protocol's own (not the DRM structs): the wire
 * ABI stays fixed whatever the DRM uapi snapshot does. The client library fills
 * them from drm_v3d_submit_cl / _tfu / _csd field by field. */
typedef struct {
	uint32_t bcl_start, bcl_end;   /* binner control list [start, end) */
	uint32_t rcl_start, rcl_end;   /* render control list [start, end) */
	uint32_t qma, qms;             /* tile-allocation memory address / size (CT0QMA/QMS) */
	uint32_t qts;                  /* tile-state address (CT0QTS), 0 = none */
	uint32_t flags;                /* V3DA_CL_* */
} v3da_cl_desc_t;

#define V3DA_CL_FLUSH_CACHE (1u << 0)   /* == DRM_V3D_SUBMIT_CL_FLUSH_CACHE */

typedef struct {
	uint32_t icfg, iia, iis, ica, iua, ioa, ios;
	uint32_t coef[4];
} v3da_tfu_desc_t;

typedef struct {
	uint32_t cfg[7];               /* CSD_QUEUED_CFG0..6; CFG0 kicks */
	uint32_t coef[4];              /* unused on V3D 4.2 (DRM parity) */
} v3da_csd_desc_t;

/* A syncobj reference inside a submit (legacy in/out_sync fields and the
 * MULTI_SYNC extension, flattened). */
typedef struct {
	uint32_t handle;
	uint32_t flags;         /* V3DA_SEM_* */
} v3da_sem_t;

#define V3DA_SEM_RENDER (1u << 0)   /* in-sync: the RENDER job waits (CL in_sync_rcl); else the first job */

/*
 * SUBMIT_CL / TFU / CSD: v3da_submit_t in the request's u, and a flat buffer in
 * msg.i.data (the library keeps it page-aligned - E5: every unaligned end costs a
 * shadow page and a copy):
 *     [ v3da_cl_desc_t | v3da_tfu_desc_t | v3da_csd_desc_t ]  desc_size bytes
 *     [ uint32_t bo_handles[nbo]         ] every BO the job touches: WAIT_BO and the
 *                                          in-flight references depend on it
 *     [ v3da_sem_t in[nin]               ] resolved to fences AT SUBMIT TIME (DRM)
 *     [ v3da_sem_t out[nout]             ] get the (last) job's fence
 * The reply's u holds v3da_submit_resp_t. The server copies what it keeps before
 * it replies: the window is gone afterwards.
 */
typedef struct {
	uint32_t desc_size;
	uint32_t nbo;
	uint32_t nin;
	uint32_t nout;
	uint32_t flags;         /* reserved, 0 */
	uint32_t pad;
} v3da_submit_t;

typedef struct {
	v3da_fence_t first;     /* CL: bin fence; TFU/CSD: the job's fence */
	v3da_fence_t last;      /* CL: render fence; TFU/CSD: same as first */
} v3da_submit_resp_t;

#define V3DA_SUBMIT_MAX_BOS  4096u
#define V3DA_SUBMIT_MAX_SEMS 16u


/* ------------------------------------------------------------------------- */
/* Envelopes                                                                  */
/* ------------------------------------------------------------------------- */

typedef struct {
	uint32_t magic;         /* V3DA_MAGIC */
	uint32_t op;            /* enum v3da_op */
	uint32_t flags;
	uint32_t pad;
	union {
		uint8_t raw[48];
		v3da_get_param_req_t get_param;
		v3da_bo_create_req_t bo_create;
		v3da_bo_req_t bo;
		v3da_nop_req_t nop;
		v3da_fence_wait_req_t fence_wait;
		v3da_syncobj_req_t syncobj;
		v3da_irq_mode_req_t irq_mode;
		v3da_bo_checksum_req_t bo_checksum;
		v3da_submit_t submit;
		v3da_syncobj_import_req_t syncobj_import;
		v3da_scanout_req_t scanout;
		v3da_flip_req_t flip;
		v3da_mode_req_t mode;
		v3da_qstats_req_t qstats;
	} u;
} v3da_req_t;

typedef struct {
	int32_t err;            /* 0 or a negative errno */
	uint32_t op;            /* echo */
	union {
		uint8_t raw[56];
		v3da_info_t info;
		v3da_get_param_resp_t get_param;
		v3da_bo_create_resp_t bo_create;
		v3da_bo_resp_t bo;
		v3da_fence_resp_t fence;
		v3da_fence_wait_resp_t fence_wait;
		v3da_syncobj_resp_t syncobj;
		v3da_irq_mode_resp_t irq_mode;
		v3da_irq_selftest_resp_t irq_selftest;
		v3da_bo_checksum_resp_t bo_checksum;
		v3da_stats_t stats;
		v3da_quit_resp_t quit;
		v3da_submit_resp_t submit;
		v3da_scanout_resp_t scanout;
		v3da_flip_resp_t flip;
		v3da_mode_resp_t mode;
		v3da_qstats_q_t qstats_q;
		v3da_qstats_g_t qstats_g;
	} u;
} v3da_resp_t;


#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(sizeof(v3da_req_t) == 64, "v3da_req_t must be exactly msg.i.raw[64]");
_Static_assert(sizeof(v3da_resp_t) == 64, "v3da_resp_t must be exactly msg.o.raw[64]");
_Static_assert(sizeof(v3da_hello_t) <= 48, "HELLO must fit i.raw after the 16-byte ioctl_in_t header");
_Static_assert(sizeof(v3da_memref_t) == 24, "memref layout is ABI");
_Static_assert(sizeof(v3da_fence_t) == 16, "fence layout is ABI");
_Static_assert(sizeof(v3da_fence_hdr_t) == V3DA_FENCE_HDR_SIZE, "fence page header size");
_Static_assert(sizeof(v3da_fence_slot_t) == V3DA_FENCE_SLOT_SIZE, "fence page slot size");
_Static_assert(sizeof(v3da_fence_page_t) <= V3DA_FENCE_PAGE_SIZE, "fence page overflows one page");
_Static_assert(sizeof(v3da_stats_t) <= 56, "stats must fit o.raw");
_Static_assert(V3DA_Q_COUNT <= 8, "fence header arrays hold 8 queues");
_Static_assert(sizeof(v3da_qstats_q_t) <= 56, "qstats must fit o.raw");
_Static_assert(sizeof(v3da_qstats_g_t) <= 56, "qstats must fit o.raw");
_Static_assert(sizeof(v3da_submit_resp_t) <= 56, "submit reply must fit o.raw");
#endif


#endif /* _V3DA_PROTO_H_ */
