# SMP status + overnight work plan (2026-07-13)

## Verdict from code analysis: true 4-core SMP is ALREADY implemented

The scheduler is **not** cpu0-only. The code implements a **global run-queue with
per-CPU current-thread tracking**, all cores run the same scheduler and pull from
the same queue, there is **no thread affinity**, and each core preempts on its own
**per-CPU banked CNTV timer**. Decisive evidence (kernel `sources/phoenix-rtos-kernel`):

- `proc/threads.c:45-50` — `thread_t *ready[8]` (global per-priority queues) +
  `thread_t **current` (per-CPU). `_proc_current()` = `current[hal_cpuGetID()]`
  (threads.c:538). Every core runs `_threads_schedule`, pulls from the same
  `ready[]` (437-444), stores its pick in `current[cpuId]` (462).
- No affinity fields anywhere in `proc/threads.h`; `proc_threadCreate` sets no CPU
  binding. The only CPU mask is IRQ routing `DEFAULT_CPU_MASK=0xF` (all 4 cores).
- Per-core timer: `cntv_tval_el0`/`cntv_ctl_el0` are banked; each core arms its own
  (aarch64.h:128,135); `threads_timeintr` re-arms *that core's* CNTV (threads.c:241).
- Locking is real multi-core: `threads_common.spinlock` (ldaxrb/stxrb, spinlock.c:52)
  + `schedulerLocked` (hal.c:63) released post-context-restore in asm.
- Bring-up: `NUM_CPUS=4U`; secondaries do per-CPU init + `daifClr` + take timer PPIs
  (_init.S:832-847); `main()` publishes `hal_smpPrimaryReady` (main.c:161-176).

## "Validated" (2026-05-25) measured actual distribution, not just idle reachability
- `docs/done/2026-05-25-smp-phase-e-validated.md`: 4 `[idle]` threads accrued Σ≈4×
  wall-clock cpuTime → all 4 cores independently scheduling.
- `docs/done/2026-05-25-smp-phase-e-saturation.md`: 4 CPU-bound burn threads Σ≈3.77×
  wall-clock → ~94% concurrent on 4 cores. Direct non-idle distribution proof.

## Why the user believed "cpu0-only"
- **README.md:40** says "scheduler is cpu0-only" — STALE (predates the 2026-05-27 fix).
- Likely also a misread of the diag line `smp: tick+15s cpu0=15242 cpu1=5038 ...`
  (status.md:800): the asymmetry is `hal_smpFirstIntervalUs=10000000` (main.c:171)
  deferring each secondary's FIRST tick 10 s so cpu0 finishes boot — startup deferral,
  not cpu0-only scheduling.

## IMPORTANT caveat → runtime re-verification required
The phase-e distribution numbers were captured on a DIFFERENT branch
(`agent/rpi4-genet`, 2026-05-25). The mechanism is present + correct in the current
`agent/rpi4-program-reloc` tree (verified above), but the distribution has NOT been
re-measured on the current branch. So the overnight job must runtime-verify before
declaring done or rewriting docs.

## Overnight plan (execute AFTER the flicker SD test; NFS netboot; card OUT of Pi)

1. **Add a per-thread `lastCpu` field** — set to `hal_cpuGetID()` in `_threads_schedule`
   (threads.c:462), plumb into `threadinfo_t`. This is BOTH the SMP proof (thread→core
   mapping) AND the foundation for the per-core utilization tool (task #16).
2. **Runtime-verify distribution** — spawn N>4 CPU-bound threads, measure
   Σ(Δ cpuTime)/Δ wall. Working ≈4×; cpu0-only ≈1×. (Extend diag-udp `'b'` burn +
   `'t'` threadsinfo, or a small userspace burn util.)
3. **Per-core CPU utilization tool (#16)** — kernel per-cpu busy/idle accounting exposed
   via threadsinfo/perf; a small ncurses per-core monitor (or extend Phoenix `top`/`ps`).
   Prefer this over a full htop port (procfs+ncurses heavy).
4. **Fix the docs** — correct README.md:40; reconcile the stale first half of the
   config.h comment (lines 21-35) with the working second half; confirm KNOWN-ISSUES:50
   + status.md are accurate.
5. **Optional gaps (if time, low-risk):**
   - **Wake-IPI latency:** newly-runnable threads only get picked up on a secondary's
     next 1 ms tick (no reschedule-SGI). `TIMER_WAKEUP_IRQ` IPI path is compiled out
     (undefined macro). Adding an aarch64 reschedule-SGI (`hal_cpuSendIPI` exists,
     interrupts_gicv2.c:435) would cut dispatch latency. Correctness-safe either way.
   - **Userspace cross-core visibility anomaly** (saturation-doc bonus finding): burn
     counters written on cpu1-3 read 0 from cpu0 — possible outer- vs inner-shareable
     user page mappings (hal/aarch64/pmap.c). Kernel scheduling unaffected. Needs
     runtime confirmation; only matters for lock-free userspace SMP code.
   - Cold-boot core-release nondeterminism (no PSCI force-release fallback).
