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

## 2026-05-23 update — D-6 binary search narrows the bug below threads_schedule

Two experiments ran today:

1. `6e7669ca` (cpu1-only `daifClr #7`): cpu1 unmasks IRQ, cpu2/3
   stay masked. Primary hangs at "hi: primary-ready set" /
   inside hal_cpuReschedule.
2. `49d06558` (D-6 bypass kill switch): all secondaries unmask
   `daifClr #7`, but `hal_smpSkipScheduler=1` makes
   `threads_timeintr` return 0 on cpu_id != 0 — secondaries
   never enter `threads_schedule`. Primary STILL hangs at the
   same point.

The fact that experiment #2 hangs identically eliminates the
scheduler hot-path itself as the proximate cause. **The break
happens in IRQ entry/exit mechanics on the secondary CPU
between vbar_el1 dispatch and the post-EOI eret.** The bug is
below `threads_schedule` — it is in some interaction between
secondary IRQ servicing and primary's concurrent execution of
`main()` / `hal_cpuReschedule`.

Reverted with `c7706f80`. HEAD-1 (`6e7669ca`) holds the cpu1-only
experiment commit; HEAD is back to D-5 milestone (all secondaries
parked in WFI with DAIF masked).

### Refined hypothesis list

Eliminated:
- (#1) Per-CPU current[] not initialised in scheduler entry.
- (#5) Per-CPU idle threads not pre-assigned.
- "Multi-CPU contention" — one secondary alone is enough.
- "threads_schedule SMP-safety" — bypass test still hangs.

Still possible:
- (#4) `hal_lockScheduler` wfe-spinlock state. Secondary
  doesn't write `schedulerLocked` in D-6 path (cbz w0,1f
  skips unlock_scheduler when reschedule=0), but maybe the
  unconditional `unlock_scheduler` in `.L_el1_syscall` (line
  240 of `_exceptions.S`) clobbers it when primary EREts —
  AFTER secondary's `stxr` would have grabbed it later.
- **NEW: `interrupts_common.spinlock[27]` cache-line
  contention with `hal_started() == 0`.** When `hal_started`
  is 0, hal_spinlockSet takes the FAKE-LOCK path (just
  daifSet, no ldaxr/stxr). Both cpus enter
  `interrupts_dispatch` with NO real lock, racing on
  `counters[]`, `handlers[]`, and the call chain. cpu0's
  first timer IRQ fires concurrently with cpu1's first
  timer IRQ.
- **NEW: bic NO_INT in `.L_el1_syscall` (line 221).** This
  removes the I-mask from saved SPSR of the boot context.
  After threads_schedule + unlock_scheduler + eret, cpu0
  unmasks IRQ. The pending timer IRQ fires *immediately*.
  cpu0 then re-enters `_interrupts_dispatch` with a *boot*
  context that may have stale fields, leading to undefined
  state.
- **NEW: pending PPI bit on cpu0** between
  `_hal_timerInit` (which programs CNTV) and the FIRST
  reschedule. CNTV deadline passes long before the
  reschedule; the IRQ is *queued* on cpu0's GICC the entire
  time main() runs with DAIF masked. cpu0's
  `hal_cpuReschedule` + eret to new thread causes the
  queued IRQ to fire as the very first instruction in the
  new thread context.

### Smallest next experiments

A. Move `msr daifClr, #7` BEFORE the per-CPU bring-up chain
   on secondaries — i.e. test whether the failure is in the
   GIC distributor write paths of `_hal_interruptsInitPerCPU`
   (vs the IRQ handler itself). Should regress equally
   if it's GICD; should be clean if it's the handler.

B. Replace `daifClr #7` with `daifClr #4` (unmask SError
   only, keep I+F masked) and verify primary still works.
   This separates IRQ servicing from SError handling.

C. Disable the bic NO_INT in `.L_el1_syscall` and observe.
   This keeps DAIF masked across the boot reschedule and
   into the new thread context — IRQs only unmask via
   `_hal_start()` later. If primary survives, the
   "first-eret-fires-pending-PPI" hypothesis holds.

D. Make primary mask its own timer PPI (gicd disable) until
   the first scheduler context is fully entered, similar to
   how Linux defers IPI/PPI enable. Reduces collision with
   secondary's first IRQ.

E. Audit `hal_started` gating on hal_spinlockSet. If the
   fake-lock path is incorrect for any spinlock used in
   IRQ context, real locks must be used from day 0 in SMP
   mode. interrupts_common.spinlock[n] in particular is
   problematic.

Experiment E is the most promising because it explains
WHY a known-correct serialization (spinlock[27]) fails to
serialize cpu0 and cpu1's IRQ handlers — they're both
running with the no-op fake-lock during boot.

## 2026-05-23 #2 update — experiments E and I results, D-7 milestone landed

### Experiment E (kernel `9ffeb85d`) — real spinlocks from day 0

Removed the `hal_started() == 0` fake-spinlock path in
`hal/aarch64/spinlock.c`; always use ldaxr/stxr real locks
in SMP builds. Combined with the daifClr + bypass kill switch
from D-6. **Primary STILL hangs at "hi: primary-ready set".**
Eliminates the fake-spinlock hypothesis.

The real-spinlock change is kept on HEAD as an independent
correctness improvement.

### Experiment I (kernel `3976f7ac`) — skip CNTV arm on secondaries

Commented out `bl _hal_timerInitPerCPU` in `_other_core_virtual`.
The timer PPI bit is still enabled in `GICD_ISENABLER0`, but
with CNTV disabled the PPI line never asserts, so even
`daifClr #7` lets the secondary stay in WFI.

**Primary boots cleanly.** Full UART signature on hardware:

    smp: intr cpu0=1 cpu1=1 cpu2=1 cpu3=1
    smp: ppi  cpu0=0 cpu1=1 cpu2=1 cpu3=1
    smp: tmr  cpu0=0 cpu1=0 cpu2=0 cpu3=0   <- skipped
    smp: tick cpu0=N cpu1=0 cpu2=0 cpu3=0
    fbcon: ok
    usb-hcd: ops->init fail rc=-19            <- separate USB issue

So the proximate cause of the D-6 hang is **timer-PPI delivery
on a secondary**, not any of the earlier-investigated paths
(scheduler hot-path, threads_schedule, hal_started spinlock
fallback, fake locks, multi-CPU contention, current[] init,
or idle-thread preallocation).

The "what about IRQ delivery itself disturbs primary?" mystery
remains. Specifically:
  - GICC IAR/EOI are banked. Secondary EOI shouldn't reach
    primary's CPU interface.
  - The handler-chain in `interrupts_dispatch` reads
    `interrupts_common.counters[]` / `handlers[]` under the
    spinlock (now real). cpu0's RW of these doesn't depend on
    secondary's RW.
  - With the D-6 bypass kill switch, the secondary returns 0
    from `threads_timeintr` and never enters
    `threads_schedule` / pmap_switch / TLBI. Primary still
    hangs.

Open hypotheses for Phase D-8:
  H1. Secondary's `_set_up_vbar_and_stacks` writes TTBR0 but
      doesn't TLBI. Subsequent eret-to-WFI on first IRQ uses
      stale TLB entries. Possibly fine, possibly not.
  H2. BCM2711 A72 erratum involving CNTV + multi-core. Linux
      and Raspberry Pi OS may have a CPUACTLR_EL1 workaround
      we're missing (we have 1319367 from the armstub, but
      something else may apply on the kernel side).
  H3. CNTV_TVAL programming triggers an asynchronous abort or
      SError that the secondary's exception handler doesn't
      route correctly — corrupting cpu0 via some shared
      coherency state.
  H4. Phoenix-armstub spin-table protocol leaves secondaries
      with a stale CNTV_CTL value (e.g. EnableMask=1 with stale
      CVAL) — first arming triggers immediate interrupt before
      the GIC config is fully consistent across CPUs.

### Decision (2026-05-23)

Lock in the working configuration as a stable milestone (kernel
`bb5e158f`, manifest
`manifests/2026-05-23-d7-4cpu-enum-cpu0-scheduler.md`) and
pivot focus to USB work where there's tractable progress
(rc=-19 visible in experiment I — first time the USB stack
gets far enough to report from real hardware). D-8 returns
once USB and other higher-priority items are advanced.
