# #67 residual torch/monster mangle — Linux vs Phoenix SUBMIT ORDERING analysis (2026-07-26, read-only)

Read-only research. Question posed: **Linux runs Quake on the same V3D 4.2 with the same cache
ops and is glitch-free — so Linux must have an ORDERING / sequencing guarantee that the Phoenix
winsys lacks. Find it.** Established facts I build on (not re-litigated):

- The residual #67 glitch (wall-torch flames + some monsters render as boot-varying mangled
  spikes; guns fixed) is a **consumer render RACE**, not a producer/data bug: VBO source bytes
  byte-identical across 5 cold boots, output still mangle-varies (see
  `docs/inprogress/2026-07-26-gpu-torch-producer-analysis.md` §2, §3).
- On V3D 4.2 the port's cache-maintenance **opcodes already match Linux exactly**: `v3d_flush_l3`
  / `v3d_invalidate_l2c` are version-gated no-ops, leaving bare `L2TFLS` (FLM=0) + `SLCACTL =
  0x0f0f0f0f` (same doc §1). The fix is **not** a missing cache op.
- `SLCACTL` slice-cache invalidate is **fire-and-forget** on 4.2 (no busy/completion bit). fix-A
  (a waited-L2T-flush after SLCACTL, before the CT0 kick, winsys 958-960) only injects LATENCY;
  it fixed large models (guns) and 8/9 tested frames but not the tiny/heaviest ones (torch F0070).
  fix-A is **committed + HW-validated** (`agent/quake-67-gpu-coherency-fix` 214be9a).

**Headline finding (Deliverable 3, stated honestly up front):** Linux does **not** have a proven
hard "guarantee" Phoenix lacks. What Linux has is a **much larger, partially hardware-floored
settle window** between the `SLCACTL` write and the binner's first vertex fetch than the Phoenix
winsys does — and, critically, Phoenix's own per-submit `wait-new` (winsys 939) *removes* the one
HW-interlock (the in-flight L2T flush self-stall) that Linux leaves in place. Whether that window
is a true guarantee rests on two **undocumented** assumptions that this port's own bin→render
handoff experience partially contradicts. Details and the risk-graded fix candidates below.

---

## Deliverable 1 — the FULL Linux submit→execute path for a CL (binner) job

All in `external/linux/drivers/gpu/drm/v3d/`.

### 1a. Where the invalidate sits relative to the CT0 kick — `v3d_bin_job_run` (v3d_sched.c:211-270)

`v3d_bin_job_run` is the DRM scheduler's `run_job` callback (registered at v3d_sched.c:819).
It runs **synchronously in the scheduler's worker thread** — there is **no IRQ round-trip**
between the invalidate and the kick. The exact sequence:

| Step | Line | Action |
|---|---|---|
| 1 | 235 | `V3D_CORE_WRITE(0, V3D_PTB_BPOS, 0)` — clear overflow (under `queue_lock`) |
| 2 | **238** | **`v3d_invalidate_caches(v3d)`** — L2T flush (issue only) + SLCACTL slice invalidate |
| 3 | 240 | `fence = v3d_fence_create(v3d, V3D_BIN)` — alloc + spinlock (CPU work) |
| 4 | 244-246 | `dma_fence_put` / `dma_fence_get` — atomic refcount ops |
| 5 | 248 | `trace_v3d_submit_cl(...)` — tracepoint |
| 6 | 251 | `v3d_job_start_stats(...)` — `preempt_disable` + 3× `write_seqcount` (each an `smp_wmb`) + `local_clock()` + `preempt_enable` |
| 7 | 252 | `v3d_switch_perfmon(...)` — compare, possibly perfmon MMIO writes |
| 8 | 257-260 | `V3D_CORE_WRITE CT0QMA`, `CT0QMS` — MMIO writes |
| 9 | 261-265 | `V3D_CORE_WRITE CT0QTS` (if `job->qts`) — MMIO write |
| 10 | 266 | `V3D_CORE_WRITE CT0QBA` — MMIO write |
| 11 | **267** | **`V3D_CORE_WRITE CT0QEA`** — MMIO write; **writing CT0QEA is what starts the job** (comment 254-256) |

So between the last invalidate write (SLCACTL, step 2) and the job kick (CT0QEA, step 11) Linux
does: a fence allocation, atomic refcount ops, a tracepoint, `v3d_job_start_stats` (preempt
gymnastics + three seqcount memory barriers + a `local_clock()` read), a perfmon switch, and
**~3–4 MMIO register writes** (CT0QMA/QMS[/QTS]/QBA). All of this is wall-clock time during which
the fire-and-forget slice invalidate proceeds on the GPU.

### 1b. What `v3d_invalidate_caches` actually does on 4.2 — and the L2T flush is NOT waited

`v3d_invalidate_caches` (v3d_gem.c:249-261), outside-in order (comment 252-256):

1. `v3d_flush_l3` (145-159): whole body under `if (ver < V3D_GEN_41)` → **no-op on 4.2**.
2. `v3d_invalidate_l2c` (164-173): `if (ver >= V3D_GEN_33) return;` → **no-op on 4.2**.
3. `v3d_flush_l2t` (176-191): `L2TCACTL = L2TFLS | FLM_FLUSH`. `FLM_FLUSH == 0` (v3d_regs.h:264),
   so a **bare `L2TFLS`**. **Issue only — NO wait for completion.** Its comment (179-185) is the
   load-bearing one:
   > "While there is a busy bit (V3D_L2TCACTL_L2TFLS), we don't need to wait for completion before
   > dispatching the job — **L2T accesses will be stalled until the flush has completed.**"
4. `v3d_invalidate_slices` (239-247): `SLCACTL = 0xf` in each of TVCCS/TDCCS/UCC/ICC =
   **`0x0f0f0f0f`** — fire-and-forget, no busy bit, no wait. **This is the LAST write in the
   invalidate.**

Linux waits on `L2TFLS` in **exactly one** place — `v3d_clean_caches` (v3d_gem.c:201-236,
lines 211-215 / 228-231), which is the **CACHE_CLEAN** queue that runs after **CSD** compute jobs
(v3d_sched.c:694-708, 842-845). It is **never** in the bin or render CL path. So on the CL submit
path Linux **never** waits the L2T flush — it relies entirely on the HW self-stall (step 3
comment).

### 1c. Is SLCACTL per-job on 4.2, or only at context/exec boundaries? — PER JOB; not a Phoenix-only addition

`v3d_bin_job_run` calls `v3d_invalidate_caches` **every bin job** (v3d_sched.c:238), and
`v3d_invalidate_caches` **always** calls `v3d_invalidate_slices` (v3d_gem.c:260, no version gate).
The render path does the same: `v3d_render_job_run` calls `v3d_invalidate_caches` before the CT1
kick (v3d_sched.c:292 → 313-314), with the comment (286-291) explaining why the per-render
invalidate is needed even though bin already invalidated. CSD also invalidates per job
(v3d_sched.c:382).

**Conclusion:** the per-CL-submit SLCACTL invalidate is **standard Linux behavior, not a Phoenix
addition.** Both drivers invalidate the slice caches once per bin job (and again per render job),
via the same register at the same granularity. So the difference is **not** *whether* or *how
often* the invalidate happens — it is purely the **ordering/timing between the invalidate and the
job kick**.

---

## Deliverable 2 — contrast with Phoenix `ioc_submit_cl`, and the exact ordering difference

Phoenix `tools/v3d-driver-port/v3d_phoenix_winsys.c`, `ioc_submit_cl`. Baseline pre-bin sequence
(constants: `CTL_L2TCACTL=0x0030`, `L2TCACTL_L2TFLS=1<<0`, `CTL_SLCACTL=0x0024`,
`SLCACTL_INVAL_ALL=0x0f0f0f0f`, winsys 76-81):

| Line | Action |
|---|---|
| 912 | `dsb sy` — drain CPU stores to uncached BOs to DRAM |
| 919 | `mmu_flush_tlb(h)` — MMUC flush + TLB clear, **each spin-waited** (773-780) |
| **937** | `l2t_flush_wait(c0)` — **wait-old** (spin until any prior `L2TFLS` idle; GFXH-1897, 754-761) |
| **938** | `c0[CTL_L2TCACTL] = L2TCACTL_L2TFLS` — issue L2T flush |
| **939** | `l2t_flush_wait(c0)` — **wait-new: spin until THIS flush COMPLETES** ← the divergence |
| **940** | `c0[CTL_SLCACTL] = SLCACTL_INVAL_ALL` — slice invalidate |
| 958-960 | **fix-A**: `l2t_flush_wait; L2TFLS; l2t_flush_wait` (extra waited flush after SLCACTL) |
| 962-963 | `INT_CLR`, `PTB_BPOS` writes |
| 964-966 | `CT0QMA/QMS[/QTS]/QBA/QEA` writes — **CT0QEA kicks the job** |

### The specific ordering difference

Both drivers issue the same two ops in the same outside-in order (L2T flush, then SLCACTL) and
then kick. The difference is **what state the L2T flush is in when the binner starts, and how
big the SLCACTL settle window is:**

- **Linux** issues the L2T flush and *leaves it in flight* (never waits it, §1b). SLCACTL is issued
  ~immediately after. Then a fence alloc + stats/perfmon CPU work + ~3–4 MMIO writes happen, then
  the kick. When the binner starts, it must fetch its control list from memory **through L2T** —
  and per the self-stall property (v3d_gem.c:179-185) that fetch **stalls in hardware until the
  L2T flush completes.** So the binner physically cannot read a single CL word (let alone reach
  the vertex packet) until the L2T flush retires. The SLCACTL invalidate — issued back-to-back
  with the L2T flush — has *at least* that whole L2T-flush duration, plus the CPU work, plus the
  CL-prologue execution, to settle.

- **Phoenix baseline** does the opposite at line 939: it **spin-waits the L2T flush to
  completion** *before* issuing SLCACTL and *before* the kick. So at kick time the L2T is already
  clean, the binner's CL fetch **does not self-stall**, and it proceeds at full speed toward the
  vertex fetch. The SLCACTL invalidate (issued at 940) has only the ~5–6 intervening MMIO writes
  (962-966) as its settle window — no HW-interlocked floor. On a fast cold boot the coordinate
  shader's first vertex fetch beats the slice invalidate → stale TVCCS/TDCCS read → collapse.

**The irony worth stating precisely:** Phoenix's line-939 `wait-new` was added for CL/vertex read
coherency ("the flush must complete before the bin reads its CL/vertex data") — but per the L2T
self-stall property that wait is **redundant** (the HW already stalls the read), and it is
**harmful** to the SLCACTL race because completing the L2T flush *before* the kick discards the
very self-stall window that in Linux naturally covers the fire-and-forget slice invalidate.
fix-A (958-960) then re-adds a waited flush *after* SLCACTL to buy the settle time back — but as
CPU-spin latency, not as the HW interlock.

**Why fix-A is fragile where Linux's ordering is sturdier.** fix-A races a **GPU** operation (the
slice invalidate) against a **CPU spin** (`l2t_flush_wait` polling MMIO) — two different clock
domains whose relative speed shifts with cold-boot V3D clock/thermal phase, so the heaviest frame
(F0070) still occasionally loses. Linux's in-flight-L2T-flush ordering instead pits the slice
invalidate against the **L2T flush** — two **GPU** operations started back-to-back — and the
binner's read is gated on the *slower* of the two (L2T flush) completing. That comparison does not
depend on CPU speed. (This is the strongest available "structural" argument — but see Deliverable
3 for why it is *not* a proven guarantee.)

---

## Deliverable 3 — honest assessment: window, not proven guarantee; two undocumented assumptions

The elegant framing ("two GPU ops racing, one structurally faster ⇒ deterministic") rests on
**two undocumented assumptions**, and this port's own code contains a **local counterexample** to
the first:

**Assumption (a): the L2T self-stall is honored for reads on THIS silicon.** The whole
"guarantee" needs the binner's CL/vertex read to actually stall on the in-flight L2T flush. The
only *local* evidence about whether this Pi's V3D honors that is the **bin→render handoff**
(winsys 1046-1058): the code comment records that when the handoff L2T flush was issued but **not
waited**, CT1 read a stale/incomplete tile-list across the in-flight flush and **wedged ~50% of
boots**, and adding `wait-new` there fixed it. The Linux comment says "L2T *accesses* will be
stalled" (reads included) — so if the self-stall reliably held, CT1 would not have wedged. That
is a **direct local contradiction** of assumption (a).

  *Possible reconciliation (plausible, NOT established from here):* the handoff is a
  **producer→consumer** flush — the binner *wrote* tile-lists that CT1 must read, and `FLM_FLUSH=0`
  is *invalidate*, not clean (v3d_regs.h:264-268), so the handoff may be a **write-drain/visibility**
  problem the read-stall does not address, whereas the pre-bin case is a pure **read-side**
  invalidate of stale prior-frame lines — exactly what the self-stall comment covers. This is
  believable but it is a hypothesis about undocumented silicon, not a fact.

**Assumption (b): slice-invalidate duration ≤ L2T-flush duration.** For the binner's L2T-gated
first read to imply "slice invalidate already done," the small L1 slice caches must invalidate no
slower than the whole L2T flushes. Architecturally plausible (L1 slice caches are far smaller than
the unified L2T) but **undocumented**.

**So the truthful answer to the task question is:** Linux is glitch-free here not because of a
proven ordering *guarantee* Phoenix lacks, but because Linux's submit path gives the fire-and-forget
slice invalidate a **far larger settle window** — an in-flight L2T flush that (if the self-stall
holds) hardware-floors the binner's first read, **plus** genuine CPU work (fence alloc, seqcount
stats, perfmon, MMIO) between invalidate and kick that Phoenix does not perform. Phoenix's window is
both smaller *and* actively shrunk by its own line-939 `wait-new`. Closing #67's residual is about
**restoring that window** (ideally with the HW floor), while honestly acknowledging neither (a) nor
(b) is guaranteed — the HW test decides.

---

## Deliverable 4 — is Phoenix invalidating the WRONG / insufficient thing? NO.

Ruled out. Linux invalidates the vertex/slice caches via the **same register, same granularity**
as Phoenix:

- Linux `v3d_invalidate_slices` (v3d_gem.c:239-247) writes **`V3D_CTL_SLCACTL`** (offset `0x24`,
  v3d_regs.h:251) with `0xf` in each of `TVCCS` (bits 27:24), `TDCCS` (19:16), `UCC` (11:8),
  `ICC` (3:0) = **`0x0f0f0f0f`** (v3d_regs.h:252-259).
- Phoenix writes the identical value to the identical offset: `CTL_SLCACTL=0x0024`,
  `SLCACTL_INVAL_ALL=0x0f0f0f0f` (winsys 80-81, 940).

There is **no separate TFU-based or CSD/CLE-based vertex-cache invalidate** on 4.2 — the TFU path
(v3d_sched.c:319-364) touches only TFU_* registers and does no cache invalidate at all; CSD
(v3d_sched.c:366-413) uses the *same* `v3d_invalidate_caches` (line 382). And Linux's full
`v3d_invalidate_caches` on 4.2 reduces to exactly `L2T flush + SLCACTL` (§1b) — which is exactly
what Phoenix does. **Phoenix is invalidating the correct and complete set. The problem is purely
timing/ordering, not the target of the invalidate.**

---

## Best-candidate winsys changes (described only — NOT edited)

Two candidates, risk-graded. **fix-A (winsys 958-960) is committed + HW-validated at 8/9 and must
be retained as the fallback baseline in BOTH** — the sibling producer-analysis doc's verdict
("fix-A is additive at most, NOT a replacement") stands unless the HW test says otherwise.

### PRIMARY (low-risk, zero-fps-cost): issue SLCACTL EARLIEST, keep every existing wait

**Location:** `ioc_submit_cl`, move the slice-cache invalidate to the very top of the per-submit
setup — right after the `dsb sy` (winsys 912) and *before* `mmu_flush_tlb` (919), the L2T
wait/flush/wait (937-939), and fix-A. Concretely: emit `c0[CTL_SLCACTL] = SLCACTL_INVAL_ALL;`
once at ~line 913, and **leave lines 937-940 and 958-960 unchanged** (the 940 SLCACTL becomes
redundant-but-harmless; it can stay or be dropped — dropping it is the only deletion).

**Why it works and why it is durable, not just latency:** everything already on the pre-bin path
between line 913 and the CT0QEA kick is **free settle time that is happening anyway** at zero
added cost — the two spin-waited `mmu_flush_tlb` loops (777, 779), the L2T `wait-old`/issue/`wait-new`
(937-939), fix-A's waited flush (958-960), and the ~6 MMIO writes (962-966). That is a **much
larger** window than fix-A alone provides, and it costs **nothing extra** (no new spin, no fps
hit). Crucially, **nothing refills the slice caches between the early invalidate and the kick**:
the core is idle, no shader/CL runs, and none of the intervening ops (MMU flush, L2T flush, MMIO
writes) reads through or repopulates TVCCS/TDCCS. So an invalidate issued at line 913 is still in
effect at the kick. (Outside-in ordering — Linux's reason to do L2T before slices — is moot here
because there is no *concurrent* CL to pull lines back in while the core is idle; v3d_gem.c:252-256.)

**Risk:** essentially none. It removes no load-bearing wait, adds no latency, and does not depend
on the contradicted self-stall (a) or on (b). It is still fundamentally a *latency* mitigation
(a bigger window, not a HW floor), so it may not fully close F0070 — but it strictly dominates
fix-A's window at zero cost and carries no regression risk. **Recommended first build.**

### ALTERNATIVE (higher upside, higher risk): mirror Linux's ordering exactly (in-flight L2T flush)

**Location + change:** make the pre-bin invalidate byte-for-byte match Linux `v3d_invalidate_caches`:
keep `l2t_flush_wait` **wait-old** (937) for GFXH-1897, issue the L2T flush (938), issue SLCACTL
(940) — but **delete the line-939 `wait-new`** and **delete fix-A (958-960)**, so the L2T flush is
**left in flight across the SLCACTL and the kick**, exactly as Linux does. The binner's first CL
read then self-stalls on the in-flight flush (if assumption (a) holds), hardware-flooring the
SLCACTL settle window.

**Why higher upside:** if (a) and (b) hold, this is the closest thing to Linux's actual behavior —
a HW-floored window rather than CPU-spin latency — and is the candidate most likely to close the
F0070 residual too, at *lower* fps cost than fix-A (it removes two waited flushes).

**Why higher risk (must A/B against fix-A):** it **deletes committed, HW-validated fix-A** and the
line-939 `wait-new` whose comment says it guards CL/vertex read coherency. If the L2T self-stall is
**not** honored for reads on this silicon — which the ~50%-boot handoff wedge (winsys 1055-1058)
suggests is a live possibility — then removing `wait-new` reintroduces **stale CL/vertex reads**,
i.e. a *different* failure (garbage or a bin wedge), a regression rather than a fix. GFXH-1897 stays
satisfied: the handoff `wait-old` (1059) waits this pre-bin flush idle before the next flush, and
the binner has long finished (FLDONE) by then, so the in-flight pre-bin flush is safe and bounded.

---

## Prediction tree for the orchestrator (5-boot cross-boot torch scorer)

Build ONE image per candidate; keep fix-A as the committed fallback. Frame both as **A/B test
candidates, not settled fixes.**

1. **PRIMARY (SLCACTL-earliest, fix-A retained):**
   - Torch/monster frames converge cross-boot (incl. F0070) → the bigger free window suffices;
     ship it, optionally then try the Alternative to drop fix-A for fps.
   - F0070 still varies but no worse than fix-A alone → expected (still latency, not a floor);
     keep it (zero-cost improvement) and the residual remains the deferred deep item.
   - Any regression → unexpected; revert (it only *adds* an early invalidate).

2. **ALTERNATIVE (Linux-exact, fix-A + wait-new deleted):**
   - Torch frames converge (incl. F0070) AND no new bin wedges/garbage → assumption (a) holds on
     this silicon; this is the faithful Linux fix and can *replace* fix-A (lower fps cost).
   - **Bin wedges or fresh garbage / boot-varying non-torch geometry appear** → assumption (a)
     does NOT hold (the handoff counterexample was real); **revert to fix-A**, and the self-stall
     framing is disproven — the honest conclusion becomes "Linux relies on a settle window Phoenix
     can only approximate with latency; the true fix needs a slice-invalidate completion primitive
     that V3D 4.2 does not expose (deferred)."

Watch in both: `BIN TIMEOUT` / `RENDER TIMEOUT` (winsys 1012/1104) and
`v3d_phoenix_render_recoveries` (830) — the reorder must not perturb the wedge-mitigation timing.

---

## Status

- [x] **Deliverable 1** — Full Linux bin submit path traced: invalidate at `v3d_bin_job_run`
      v3d_sched.c:238; kick (CT0QEA) at :267; **no IRQ gap** (synchronous run_job, scheduler
      thread). L2T flush **never waited** in the CL path (only in `v3d_clean_caches` for CSD,
      v3d_gem.c:201-236). SLCACTL is **per-job**, unconditional (v3d_gem.c:260) — **not a
      Phoenix-only addition.**
- [x] **Deliverable 2** — Ordering difference pinned: Phoenix **spin-waits the L2T flush to
      completion (winsys 939) before SLCACTL + kick**, removing the in-flight-L2T-flush self-stall
      that Linux keeps; Phoenix's SLCACTL settle window is only ~5–6 MMIO writes vs Linux's
      in-flight L2T flush + CPU work. fix-A re-adds settle time as CPU-spin latency (fragile:
      GPU-vs-CPU clock-domain race) where Linux's is a GPU-vs-GPU comparison.
- [x] **Deliverable 3** — Honest verdict: **not a proven guarantee** — a larger, partially
      HW-floored window resting on two undocumented assumptions ((a) L2T read self-stall honored —
      *contradicted* by the local ~50%-boot handoff wedge, winsys 1055-1058; (b) slice-invalidate
      ≤ L2T-flush duration). Do not harden the elegant framing into certainty.
- [x] **Deliverable 4** — **Wrong-thing ruled out:** Linux invalidates via the identical
      `SLCACTL=0x0f0f0f0f` (v3d_gem.c:239-247, v3d_regs.h:251-259) = winsys 80-81/940; no separate
      TFU/CSD/CLE vertex-cache invalidate on 4.2. Phoenix targets the correct, complete set.
- [x] **Candidate patches** — PRIMARY: issue SLCACTL earliest (~winsys 913), keep all existing
      waits + fix-A — bigger free window, zero fps cost, no regression risk, durable (idle core
      won't refill slice caches). ALTERNATIVE: mirror Linux exactly (delete winsys-939 wait-new +
      fix-A, leave L2T flush in flight) — higher upside/lower fps, but deletes committed fix-A and
      risks stale CL/vertex reads if the self-stall isn't honored; **A/B against fix-A.** Both keep
      fix-A as fallback. Prediction tree provided.
</content>
</invoke>
