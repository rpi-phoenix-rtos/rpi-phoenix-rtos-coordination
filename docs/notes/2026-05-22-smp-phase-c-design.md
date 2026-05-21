# SMP Phase C: scheduler entry on secondary CPUs

Status: design (not implemented). Phase A + B are in (project `cf9fbbc`,
kernel `fb9669f4`); cores 1-3 reach the kernel and park in
`_other_core_virtual`'s WFI loop with per-CPU VBAR + GIC + cpuInit.
NUM_CPUS=4 activates the multi-CPU spinlock + scheduler paths but no
threads are dispatched to secondaries yet.

## What's missing

For a secondary CPU to actually run threads, three things have to be
in place:

### 1. Per-CPU architectural timer

The ARM generic timer's `CNTV_CVAL_EL0` and `CNTV_CTL_EL0` are
banked per-CPU. `_hal_timerInit` (gtimer_timer.c) currently programs
only the primary's CVAL — secondaries' CVAL is unset and their timer
never fires.

Required:
- After `_hal_cpuInit` on a secondary, write the timer-tick wakeup
  into its CVAL_EL0 + enable bit in CTL_EL0.
- Re-arm CVAL on each timer interrupt (per the systick interval).
- Make sure the primary doesn't program secondaries' CVAL on its
  own behalf (regression check).

### 2. Per-CPU PPI enable in GIC distributor

The architectural timer's IRQ is a PPI (PPI-14 for EL1-secure virtual
timer on Cortex-A72, typically routed as IRQ 27 or 30). PPI enable
bits in `GICD_ISENABLER0` (offset 0x100) are banked per-CPU, so each
CPU must enable its own.

`_hal_interruptsInitPerCPU` currently only enables the GIC CPU
interface (GICC) — it doesn't enable any specific PPI. Required:
- Add a "register-PPI" call out of `_hal_interruptsInitPerCPU` (or
  immediately after) that enables the timer PPI for this CPU.
- Confirm that GICD_TYPER's per-CPU bits are honored on BCM2711
  (Linux's `irq-gic.c` works fine here, so it should).

### 3. Scheduler entry from `_other_core_virtual`

Currently:

    _other_core_virtual:
        bl _set_up_vbar_and_stacks
        bl _hal_interruptsInitPerCPU
        bl _hal_cpuInit
        msr daifClr, #7
    1:  wfi
        b 1b

`msr daifClr, #7` is already unmasking IRQs, so when the per-CPU
timer fires, the vector table handler runs `interrupts_dispatch`
which calls `threads_schedule` (because the timer handler returns
1 = "reschedule"). After that, `threads_schedule` would dispatch a
thread to this CPU.

So in principle the WFI loop IS the idle state: any IRQ wakes it,
the handler invokes scheduling, the new thread runs, when it sleeps
we return to WFI.

What might break:
- `threads_schedule`'s thread-picking logic uses `hal_cpuGetID()`
  to index `threads_common.current[]`. With NUM_CPUS=4 that index is
  valid (current[] is per-CPU). But the run-queue itself is
  single-shared (`threads_common.ready[]`). Multiple CPUs may race
  on the queue — covered by the (already real) spinlock around it.
- New thread creation (`proc_threadCreate`) picks an initial CPU.
  Need to confirm it picks intelligently (e.g. round-robin or "least
  loaded") and doesn't always pin to CPU 0.
- The `current[cpuId]` array must be initialized to NULL/idle for
  secondaries before they first take a timer IRQ; otherwise the
  scheduler reads garbage when it tries to save context.

## Suggested implementation order

1. **Verify timer PPI fires on secondaries first.** Add a single
   debug print in the timer ISR that includes `hal_cpuGetID()`.
   Boot with NUM_CPUS=4 and confirm we see ticks from CPU 1, 2, 3.
   If not, problem is in step 1 or 2 above. If yes, scheduler entry
   is the only missing piece.
2. **Initialise per-CPU `current[]` array.** Make sure secondaries'
   slots are NULL on first dispatch. Add an idle thread per CPU.
3. **Let secondaries fall into the scheduler from WFI.** Once
   ticks fire, the existing IRQ → `interrupts_dispatch` → 
   `threads_schedule` path should do the right thing.
4. **Validate** with a multi-CPU stress: spawn N busy threads, watch
   that they distribute across CPUs.

## Risks

- Cortex-A72 erratum 859971 (broken AT instruction) and 1319367
  (table-walk aggregation) are already worked around in the armstub
  (`phoenix-armstub8-rpi4.S`). The Phoenix-RTOS workarounds for
  these need to apply per-CPU; the armstub already does that, so
  this should be fine.
- BCM2711's PCIe and other peripherals expect specific cores. We're
  not touching peripherals here — secondaries only run scheduled
  threads. But shared peripherals like the timer interrupt MUST be
  per-CPU PPI, not SPI; double-check that.
- Multiple cores racing in the kernel could expose latent
  data-races in the kernel that single-CPU never hit. The existing
  spinlocks should cover the critical sections, but some lower-
  level paths (per-process state) may need locking added.

## Out of scope for Phase C

- USB on secondaries (the USB-HCD path is still blocked by a kernel
  pmap aliasing bug — see `2026-05-21-pcie-bridge-ageing-codex.md`).
- Power management of idle cores (cores stay at full A72 clock).
- IPI-based reschedule from primary to secondary on thread-priority
  change — nice-to-have but not strictly required for Phase C.
