# SMP Phase D-8 — experiment plan

Current HEAD: `bb5e158f` (D-7 milestone) + kernel `04ebbf38` (dormant
`hal_smpFirstIntervalUs` override).

D-8 investigates the remaining bug: arming CNTV on a secondary
breaks primary's `hal_cpuReschedule(NULL, NULL)` at
"hi: primary-ready set". The full IRQ entry/exit path is suspect
but the proximate trigger is the first timer PPI on a secondary.

Experiments are ordered by expected information gain per
hardware iteration. Each iteration costs ~25 min wall time
(Pi cooldown + boot + capture). Aim for ≤2 iterations per session.

## Experiment N — long initial CNTV interval (READY)

**Hypothesis.** Primary hangs because cpu1's first timer PPI fires
DURING primary's `hal_cpuReschedule`, racing some boot-context
state. With a long initial interval, primary finishes boot before
any secondary PPI fires; if primary then survives the later PPI
storm, the bug is window-specific.

**Activation (3 edits).**

1. `sources/phoenix-rtos-kernel/main.c`, in the existing
   `#if NUM_CPUS != 1` block (right before
   `hal_smpPrimaryReady = 1U;`), add:

       extern volatile unsigned int hal_smpFirstIntervalUs;
       hal_smpFirstIntervalUs = 10000000U;  /* 10 s */

2. `sources/phoenix-rtos-kernel/hal/aarch64/_init.S` in
   `_other_core_virtual`: uncomment `bl _hal_timerInitPerCPU`
   and replace `msr daifSet, #0xf` with `msr daifClr, #7`.

3. Rebuild + test.

**Outcomes.**
- Primary boots clean, smp:tick cpu1..3 all 0 at t<10s: setup
  good. Capture a long log (≥20 s) and watch for tick counters
  rising at ~t=10s. If primary survives that → bug is the
  boot-context window, NOT IRQ delivery in steady state.
- Primary hangs at "hi: primary-ready set" identically to D-6:
  timer race window is wider than 10s, or the bug is in arming
  itself (not delivery). Move to experiment P.
- Primary boots clean but UART silent past t=10s: PPI fired,
  handler ran, system continued — best case. Try removing the
  long-interval override entirely (back to normal SYSTICK).

## Experiment P — pre-clear CNTV_CTL_EL0 on secondaries

**Hypothesis.** Pi 4 firmware (start4.elf) may leave stale
CNTV_CTL bits on secondary cores. The first `msr cntv_ctl_el0, 1`
in `_hal_timerInitPerCPU` could combine with stale CVAL or
mask bits to immediate-fire the PPI even before the TVAL set.

**Activation.** In `_hal_timerInitPerCPU` (gtimer_timer.c), prepend:

    /* SMP D-8 P: force a known-clean CNTV control state before
     * arming, in case the firmware armstub seam left bits set. */
    sysreg_write(cntv_ctl_el0, 0u);
    hal_cpuInstrBarrier();

Then proceed with hal_gtimerStateSetWakeup as today.

**Outcomes.** Same matrix as N. If N didn't work but P does, the
bug was the dirty CNTV state. Cheap to keep.

## Experiment Q — CNTP instead of CNTV

**Hypothesis.** BCM2711 / A72 virtual-timer interaction has some
quirk our usage triggers. The physical timer (CNTP) on a separate
PPI (typically 30) might behave differently.

**Activation.** Modify `dtb_getTimerSource` to return
`dtb_timerPhys` instead of `dtb_timerVirt`, OR add a config flag.
Note the PPI number changes (must regenerate timer_common.state).

**Outcomes.** Higher information value if N + P both fail —
indicates a CNTV-specific bug. Heavier change (touches DTB
parsing) so try after N and P.

## Experiment R — CNTV armed but PPI masked in GICD

**Hypothesis.** Possibly the act of writing CNTV_CTL on a
secondary disturbs primary even without actual IRQ delivery.

**Activation.** In `_hal_interruptsInitPerCPU`, skip the
`interrupts_enableIRQ(timerIrq)` call on secondaries (cpu_id != 0)
WHILE keeping `_hal_timerInitPerCPU` enabled.

**Outcomes.** If primary survives, the IRQ delivery (not arming)
is the bad actor — narrows to GIC interaction. If primary hangs,
arming itself (the MSR to CNTV_CTL) is bad — very unusual but
possible silicon quirk.

## Experiment S — memory marker at vector entry (post-mortem)

**Hypothesis.** Need to know if secondary's IRQ vector even
enters the dispatcher.

**Activation.** Add inline asm in `exception_vector` macro
(BEFORE any stack ops), gated on `cpu_id != 0`, writing a
magic value + cpu_id to a known PA slot
(`PMAP_COMMON_SCRATCH_PAGE + 0xa00 + cpu_id*8`). After the hang,
power-cycle and have plo read those PAs and print them before
handing to kernel.

**Cost.** Requires plo modification. Highest cost; lowest priority
unless N..R all fail.

## Stop conditions

If N succeeds → D-8 is closed by the long-interval workaround.
Document the workaround, mark task #23 closer to done.

If N + P + Q + R all fail with primary-hang → bug is at a level
below user-mode code: likely silicon erratum or firmware-state
issue. At that point, the pragmatic move is to ship D-7 as the
SMP final state until a Pi 4 hardware expert can audit on
oscilloscope.

## Test budget

Each iteration: ~25 min (Pi cooldown + 5 min boot/capture). At
2 iterations per session, ~3 sessions to walk N → P → Q → R.
Experiment S adds 1-2 more sessions because plo modification is
itself a multi-step change.
