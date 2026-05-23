# SMP D-5 — scheduler investigation note (2026-05-23)

## State at HEAD

- `phoenix-rtos-kernel/4125d018` — SMP Phase D-5 milestone:
  secondaries complete full per-CPU init when DAIF stays masked.
- Boot reaches psh prompt with NUM_CPUS=4, no faults.
- Manifest: `2026-05-23-smp-d5-secondaries-init-complete.md`.

## What works

All HAL-level per-CPU init for secondaries:
1. `_set_up_vbar_and_stacks` — per-CPU stack at PMAP_COMMON_STACK
   + (cpu_id+1)*SIZE_INITIAL_KSTACK, TTBR0 = scratch, vbar_el1 =
   _vector_table.
2. `_hal_interruptsInitPerCPU` — gicc CTLR/BPR/PMR setup, banked
   PPI bit for the architectural timer enabled in GICD_ISENABLER0.
   Counter confirms all 4 cpus enter the function.
3. `_hal_cpuInit` — atomic-inc `nCpusStarted`, DSB + SEV.
4. `_hal_timerInitPerCPU` — programs banked CNTV_TVAL_EL0 +
   CNTV_CTL_EL0 with timer_common.interval.

`hal_smpPrimaryReady` flag serialises: secondaries spin at the
top of `_other_core_virtual` until primary sets it from main()
(right before `hal_cpuReschedule`). This ensures primary's
_hal_init / _vm_init / _proc_init / _syscalls_init all run
WITHOUT concurrent secondary activity.

## What breaks (and is queued)

Unmasking DAIF on secondaries (`msr daifClr, #7` in the assembly
WFI loop) lets the architectural timer PPI actually fire IRQs on
the secondary. The secondary's ISR enters `threads_schedule` via
`threads_timeintr`. Once that path is active, primary's
`main_initthr` (running on cpu0) hangs — boot stops after
`hi: primary-ready set`, never reaches `fbcon: ok`.

Both threads_schedule and proc_current are wrapped in
`hal_spinlockSet(&threads_common.spinlock, ...)`. Yet the failure
mode shows primary's main_initthr stalls when secondaries take
IRQs.

## Hypotheses to test next

1. **Per-CPU current[] not initialised in scheduler entry.**
   `threads_init` allocates `current` array and sets each slot to
   NULL. First call to `_threads_schedule` on a secondary reads
   `current[cpuId]` (NULL → no prior thread) and picks a ready
   thread. If `_proc_current()` is called *between* `current[cpuId]
   = NULL` (line 425) and the new assignment (line 465) by ANOTHER
   cpu's `proc_current` syscall, the other cpu sees NULL — harmless
   but worth verifying.

2. **lib_atomic{Inc,Dec} single-CPU implementation.** In
   `lib/lib.h` the NUM_CPUS == 1 implementations use a spinlock +
   plain memory updates. Under NUM_CPUS != 1 they use
   `__atomic_*` builtins. The C code that calls these (refcount
   updates inside scheduler / proc) should be SMP-safe. Verify no
   plain-memory updates of shared state in scheduler hot path.

3. **Console-print lock contention.** Secondaries' timer ISRs
   eventually print via klog (klog drain reads `/dev/klog`).
   Multiple CPUs in `hal_consolePutch` would contend on the
   console_common.lock spinlock. Should be safe but worth
   profiling.

4. **`hal_lockScheduler` global wfe-spinlock.** The
   `_threads_schedule` entry calls `hal_lockScheduler()` which
   on NUM_CPUS!=1 does `wfe + ldaxr` busy-wait. If two cpus
   simultaneously enter scheduler ISR, one spins on wfe. With
   primary blocked in main_initthr (cpu0 doing user/kernel
   work), if cpu0's timer ISR fires while cpu0 is in some
   non-scheduler critical section that holds the scheduler lock,
   cpu1 spins. Unclear if this would hang the system though —
   should resolve once cpu0 releases.

5. **Per-CPU idle threads not pre-assigned.** `threads_init`
   creates 4 idle threads and puts them all in
   `ready[MAX_PRIO]`. None are pre-assigned to current[cpu_id].
   On secondary's first scheduler entry, it picks ONE idle from
   the ready queue. If 4 secondaries enter scheduler in rapid
   succession, they each pick one. By the 4th, ready[MAX_PRIO]
   might be empty — secondary's scheduler asserts `selected !=
   NULL`. Worth checking if assertion fires (LIB_ASSERT).

## Smallest next experiment

Enable IRQs on cpu1 ONLY (not 2/3) and observe. If cpu1
secondary alone breaks primary, the failure is in IRQ-handling
mechanics. If 1 secondary is fine but 2+ break primary, it's
multi-CPU scheduler contention.

This needs a small _init.S tweak: conditional `msr daifClr, #7`
based on x8 (cpu_id) — only cpu1 unmasks, cpu2/3 stay masked.

## Status

Phase D-5 (HAL bring-up) DONE; Phase D-6 (scheduler SMP
safety) queued. The code at HEAD is stable and the diagnostic
chain (hi: probes + smp: counters + memory markers in armstub
and kernel _start) is in place to localise the scheduler issue
next iteration.
