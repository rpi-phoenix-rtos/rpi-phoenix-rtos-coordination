/*
 * Phoenix-RTOS
 *
 * Raspberry Pi 4 (BCM2711) V3D 4.2 asynchronous render server - internal types
 *
 * One process, one mutex (srv.lock) over clients, BOs, queues, syncobjs and parked
 * requests. The IRQ handler never takes it: it only ORs status bits into atomic
 * event words that the event thread drains. Design:
 * docs/gpu-new-lane/M1-async-render-server.md.
 *
 * Copyright 2026 Phoenix Systems
 * Author: Witold Bołt
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _V3DA_H_
#define _V3DA_H_

#include <stdint.h>
#include <sys/msg.h>
#include <sys/types.h>

#include "v3da_proto.h"


#define V3DA_MAX_CLIENTS  V3DA_FENCE_NSLOTS   /* one fence-page row each */
#define V3DA_MAX_BOS      4096u
#define V3DA_MAX_HOLES    2048u
#define V3DA_MAX_SYNCOBJS 256u                /* per client */
#define V3DA_MAX_POOL     1024u               /* pooled BO blocks */


/* ------------------------------------------------------------------------- */
/* Hardware (v3da_hw.c)                                                       */
/* ------------------------------------------------------------------------- */

typedef struct {
	volatile uint32_t *hub;
	volatile uint32_t *core0;
	volatile uint32_t *pt;        /* flat MMU page table (uncached, contiguous) */
	uintptr_t pt_pa;
	uint32_t pt_entries;
	uintptr_t scratch_pa;         /* MMU illegal-access redirect page */
	uint32_t ident[7];            /* CORE0_IDENT0/1/2, HUB_UIFCFG, HUB_IDENT1/2/3 */
	uint32_t clk_rate_hz;
	uint32_t clk_meas_hz;
	unsigned clk_tries;

	/* GPU VA allocator (bump + first-fit holes; old daemon's va_alloc) */
	uint32_t next_gpuva;
	struct { uint32_t gpuva, pages; } holes[V3DA_MAX_HOLES];
	uint32_t nholes;

	/* TLB bookkeeping: pt_gen bumps on every PTE change; tlb_gen = pt_gen at the
	 * last completed MMU flush. A quarantined BO needs tlb_gen > its clear gen. */
	uint64_t pt_gen;
	uint64_t tlb_gen;
	uint32_t tlb_flushes;

	/* Interrupt plumbing */
	unsigned irq_num;
	int irq_on;                   /* handler registered + sources unmasked */
	handle_t irq_handle;
	handle_t irq_cond;            /* == srv.cond: the handler broadcasts it */

	/* Shared with the handler (atomics only). */
	volatile uint32_t ev_core;    /* CTL_INT_STS bits seen, not yet drained */
	volatile uint32_t ev_hub;     /* HUB_INT_STS bits seen, not yet drained */
	volatile uint32_t irq_count;  /* handler invocations */
	volatile uint32_t irq_spurious;   /* reset by the event thread every loop */
	volatile uint32_t irq_count_snap; /* irq_count at the last event-thread loop */
	volatile uint32_t storm;      /* set by the handler's storm guard (1 spurious, 2 burst) */
	volatile uint32_t mmu_ctl_seen;
	/* Pre-staged binner-overflow chunk the handler may hand out on OUTOMEM
	 * (part 2; zero = nothing staged). */
	volatile uint32_t ovf_stage_va;
	volatile uint32_t ovf_stage_size;
	volatile uint32_t ovf_consumed;
} v3da_hw_t;


/* ------------------------------------------------------------------------- */
/* BOs (v3da_bo.c)                                                            */
/* ------------------------------------------------------------------------- */

enum v3da_bo_state { V3DA_BO_FREE = 0, V3DA_BO_LIVE, V3DA_BO_QUARANTINE };

typedef struct {
	int state;                    /* enum v3da_bo_state */
	uint32_t handle;              /* slot + 1 */
	uint32_t owner;               /* creating client id (0 = server) */
	uint32_t refs;                /* handle refs: creator + importers */
	uint32_t inflight;            /* jobs referencing it (part 2) */
	uint32_t flags;               /* V3DA_BO_* */
	void *cpu;                    /* server's own mapping */
	uintptr_t pa;
	uint32_t gpuva;
	uint32_t pages;
	/* last use per queue (part 2: WAIT_BO / implicit sync) */
	v3da_fence_t last[V3DA_Q_COUNT];
	/* quarantine */
	uint64_t clear_pt_gen;        /* pt_gen after the PTE clear */
	uint64_t pass[V3DA_Q_COUNT];  /* hw_submitted snapshot: wait for hw_completed >= pass */
} v3da_bo_t;

typedef struct {
	void *cpu;
	uintptr_t pa;
	uint32_t pages;
	uint32_t cached;
} v3da_pool_block_t;


/* ------------------------------------------------------------------------- */
/* Clients, syncobjs                                                          */
/* ------------------------------------------------------------------------- */

typedef struct {
	int used;
	int state;                    /* enum v3da_syncobj_state */
	v3da_fence_t fence;
} v3da_syncobj_t;

typedef struct {
	int used;
	uint32_t id;                  /* == index + 1 == the descriptor's oid.id */
	int pid;
	int hello;                    /* HELLO done */
	uint32_t slot;                /* == index */
	uint64_t next_seq[V3DA_Q_COUNT];   /* last assigned seqno per queue */
	uint32_t nparked;
	v3da_syncobj_t sync[V3DA_MAX_SYNCOBJS];
} v3da_client_t;


/* ------------------------------------------------------------------------- */
/* Jobs and queues (v3da_sched.c)                                             */
/* ------------------------------------------------------------------------- */

typedef struct v3da_job {
	struct v3da_job *next;
	int queue;                    /* enum v3da_queue */
	uint32_t client;              /* client id */
	v3da_fence_t fence;
	uint64_t t_kick_us;
	/* NOP test job */
	uint32_t delay_us;
	uint64_t done_at_us;
} v3da_job_t;

typedef struct {
	v3da_job_t *head[V3DA_MAX_CLIENTS];   /* per-client FIFO */
	v3da_job_t *tail[V3DA_MAX_CLIENTS];
	v3da_job_t *active;                   /* one job in flight */
	uint32_t rr;                          /* round-robin cursor (client index) */
} v3da_queue_t;


/* ------------------------------------------------------------------------- */
/* Parked requests                                                            */
/* ------------------------------------------------------------------------- */

enum v3da_wait_kind { V3DA_WAIT_FENCE = 1, V3DA_WAIT_SYNCOBJ, V3DA_WAIT_BO };

/* A request the server has received and not answered. Owned by exactly one list
 * (srv.waits, or a local "to answer" list after wait_claim()); whoever claims it
 * under srv.lock is its only responder. Rids are reused immediately by the kernel
 * (E5 condition 3), so a second respond would answer a stranger. */
typedef struct v3da_wait {
	struct v3da_wait *next, *prev;
	int kind;
	uint32_t client;
	msg_t msg;                    /* the received message; o.raw is filled on answer */
	msg_rid_t rid;
	uint64_t deadline_us;
	v3da_fence_t fence[V3DA_SYNCOBJ_WAIT_MAX];
	uint32_t sync_handle[V3DA_SYNCOBJ_WAIT_MAX];
	uint32_t n;
	int all;
	int for_submit;
} v3da_wait_t;


/* ------------------------------------------------------------------------- */
/* The server                                                                 */
/* ------------------------------------------------------------------------- */

typedef struct {
	uint32_t port;
	handle_t lock;
	handle_t cond;                /* event thread wakes on it (IRQ handler + dispatch) */
	int quit;
	int verbose;

	v3da_hw_t hw;

	v3da_fence_page_t *fp;        /* server's cached mapping */
	uintptr_t fp_pa;

	v3da_client_t clients[V3DA_MAX_CLIENTS];
	uint32_t nclients;
	uint64_t slot_gen[V3DA_MAX_CLIENTS];

	v3da_queue_t q[V3DA_Q_COUNT];

	v3da_bo_t bos[V3DA_MAX_BOS];
	uint32_t nbos;                /* high-water mark of slots ever used */
	v3da_pool_block_t pool[V3DA_MAX_POOL];
	uint32_t npool;

	v3da_wait_t *waits;           /* parked requests (doubly linked) */
	uint32_t nparked;
	uint32_t parked_max;

	/* poll-mode knobs */
	uint32_t poll_us;

	/* stats */
	uint32_t loops;
	uint32_t timeouts;
	uint32_t nops_done;
	uint32_t resets;
	uint32_t bo_quarantine_passed;
	uint32_t pages_to_kernel;
	uint32_t stray_fldone;        /* FLDONE with no active bin job (self-test) */
	uint32_t stray_tfuc;          /* TFUC with no active TFU job (self-test) */
} v3da_srv_t;

extern v3da_srv_t srv;


/* ------------------------------------------------------------------------- */
/* Cross-file API                                                             */
/* ------------------------------------------------------------------------- */

/* time */
uint64_t v3da_now_us(void);

/* fence-page helpers (release / acquire u64) */
static inline void v3da_store64(volatile uint64_t *p, uint64_t v)
{
	__atomic_store_n(p, v, __ATOMIC_RELEASE);
}

static inline uint64_t v3da_load64(const volatile uint64_t *p)
{
	return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}

/* v3da_hw.c */
int v3da_hw_init(v3da_hw_t *hw);
int v3da_hw_irq_enable(v3da_hw_t *hw);      /* locked; returns interrupt() rc */
void v3da_hw_irq_disable(v3da_hw_t *hw);    /* locked */
void v3da_hw_poll_status(v3da_hw_t *hw);    /* poll mode: read + W1C STS into the event words */
void v3da_hw_mmu_flush(v3da_hw_t *hw);      /* MMUC flush + TLB clear, bounded spins */
uint32_t v3da_hw_va_alloc(v3da_hw_t *hw, uint32_t pages);
void v3da_hw_va_free(v3da_hw_t *hw, uint32_t gpuva, uint32_t pages);
int v3da_hw_selftest(v3da_hw_t *hw, v3da_irq_selftest_resp_t *out);   /* called unlocked */
void v3da_hw_shutdown(v3da_hw_t *hw);       /* locked: mask + unregister */

/* v3da_param.c (separate: the DRM uapi headers clash with <sys/ioctl.h>) */
int v3da_get_param(uint32_t param, uint64_t *value);

/* v3da_bo.c */
int v3da_bo_create(uint32_t client, uint32_t size, uint32_t flags, v3da_bo_create_resp_t *out);
int v3da_bo_close(uint32_t client, uint32_t handle);
int v3da_bo_mmap(uint32_t handle, v3da_bo_resp_t *out);
int v3da_bo_offset(uint32_t handle, uint32_t *gpuva);
int v3da_bo_checksum(uint32_t handle, uint32_t off, uint32_t len, v3da_bo_checksum_resp_t *out);
void v3da_bo_client_gone(uint32_t client);
void v3da_bo_quarantine_poll(void);
void v3da_bo_counts(uint32_t *live, uint32_t *quar, uint32_t *pooled);
v3da_bo_t *v3da_bo_find(uint32_t handle);

/* v3da_sched.c */
int v3da_sched_init(void);
void v3da_event_thread(void *arg);
int v3da_submit_nop(v3da_client_t *c, uint32_t delay_us, v3da_fence_t *out);
int v3da_fence_signaled(const v3da_fence_t *f, int *error);
int v3da_fence_valid(const v3da_client_t *c, const v3da_fence_t *f);
/* Park msg as a wait. Returns 0 when parked (the server now owns the request:
 * the caller must NOT respond), 1 when already satisfied (the response fields in
 * msg->o.raw are filled; the caller responds with err 0), or a negative errno
 * (the caller responds with it). */
int v3da_wait_park(v3da_client_t *c, int kind, msg_t *msg, msg_rid_t rid,
	const v3da_fence_t *fences, const uint32_t *handles, uint32_t n, int all,
	int for_submit, uint32_t timeout_ms);
void v3da_waits_client_gone(uint32_t client, v3da_wait_t **answer);
void v3da_waits_all(v3da_wait_t **answer, int err);
void v3da_waits_answer(v3da_wait_t *list);  /* UNLOCKED: respond + free */
uint32_t v3da_jobs_inflight(void);
void v3da_jobs_client_gone(uint32_t client);
void v3da_kick_event_thread(void);          /* locked */
int v3da_slot_busy(uint32_t slot);          /* a job of this slot is still in flight */
void v3da_slot_assign(uint32_t slot);       /* new generation, zeroed row */

/* syncobjs (v3da_sched.c) */
int v3da_syncobj_create(v3da_client_t *c, uint32_t flags, uint32_t *handle);
int v3da_syncobj_destroy(v3da_client_t *c, uint32_t handle);
int v3da_syncobj_reset(v3da_client_t *c, uint32_t handle);
int v3da_syncobj_signal(v3da_client_t *c, uint32_t handle);
int v3da_syncobj_query(v3da_client_t *c, uint32_t handle, v3da_syncobj_resp_t *out);
v3da_syncobj_t *v3da_syncobj_get(v3da_client_t *c, uint32_t handle);

#endif /* _V3DA_H_ */
