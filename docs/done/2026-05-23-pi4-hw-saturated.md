# 2026-05-23 — Pi 4 hardware test bench saturated

## Symptom

After ~40 boot cycles in rapid succession across this session, the
Pi 4 hardware reaches a state where boot hangs at "vm: init done"
with NO exceptions logged, NO secondaries-related output, NO
visible progress past kernel banner+vm init.

Reverting kernel to commit `774d188d` (a known-stable state from
earlier in the session) and rebuilding does NOT fix it — boot
still hangs at the same place. So this is HARDWARE state, not
a code regression.

## Constraints discovered

- 30 min idle was enough to recover the BCM2711 PCIe bridge from
  rc=-19 poison to rc=-110 once early in the session.
- After ~40 cycles, even multiple 30-min idle gaps don't recover.
- Boot now fails earlier than USB init — primary hangs at vm
  bring-up, never reaches device probes.

## Action plan

1. **Stop testing on this Pi for the rest of the session.** Each
   cycle adds to the cumulative degradation; we're past the
   point where the hardware can recover within a useful time
   window.
2. **Real mains power-cycle (≥15 min unplugged)** before any
   further iteration. Pi 4 internal capacitors keep bridge state
   alive for longer than the relay-based power-off in
   `pi_power_off.sh`.
3. **Capture code state at last validated commit** so the next
   session knows where to pick up.

## Code state captured this session

SMP investigation milestones (in commit order):

- `phoenix-rtos-project/a74eac4` — armstub `;X<digit>` UART marker
  at start_late entry. Diagnostic discovery that the cores
  reaching armstub `in_el2` is FW-noise-masked by UART
  contention, NOT a hardware limit.
- `phoenix-rtos-project/14f081b` — armstub memory marker at PA
  0x40 + cpu_id*4. Proved all 4 cores reach armstub start_late.
- `phoenix-rtos-kernel/d5fd747b` — kernel-side `smp: am=NNNN
  k=NNNN` print + memory marker at PA 0x50 + cpu_id*4 in kernel
  _start. Proved all 4 cores reach kernel _start.
- `phoenix-rtos-kernel/56b783dc` — NUM_CPUS=4 + per-step memory
  markers at PA 0x60-0xBF in _other_core_trap/_other_core_virtual.
- `phoenix-rtos-kernel/562fb6ad` — late-bound reader in
  main_initthr via `_pmap_halMapDevice`.
- `phoenix-rtos-kernel/7d69dc7b` — TTBR0-change-after-vbar
  finding + NUM_CPUS=1 parker revert + post-vbar markers removed
  (they faulted because TTBR0 = scratch page after
  `_set_up_vbar_and_stacks`).

## Last validated boot output

Commit `562fb6ad` with NUM_CPUS=4: `smp: am=0123 k=0123` (all 4
cpus reach both armstub and kernel _start), counters
`smp: intr cpu0=1 cpu1=0 cpu2=0 cpu3=0` (only primary enters
_hal_interruptsInitPerCPU). Confirms the SMP-D-3 marker
infrastructure works on healthy hardware.

## Next session's first action

**CRITICAL: `./scripts/pi_power_on.sh` first.** The Pi was
powered off via Meross outlet at end of this session (~09:30
local). Until the next iteration powers it back on, no `psh-
interact` cycle will work — they all assume the Pi is on.

After the Pi gets a real ≥15-minute mains-off recovery period:

1. Boot validate with NUM_CPUS=1 — verify the baseline still
   reaches psh prompt.
2. Read `_init.S` lines 1002-1033 — the parker is correct but
   the post-parker code (when NUM_CPUS=4) needs the zynqmp-style
   synchronization barrier in `_hal_cpuInit` so primary blocks
   until all cpus have reached it before proceeding to vm_init.
3. Try NUM_CPUS=4 + that sync barrier. Expected outcome:
   primary's vm_init doesn't race secondaries' GIC/atomic
   activity because primary waits for them.

The work that's QUEUED for the next code change is:

```c
// In hal/aarch64/generic/generic.c, _hal_cpuInit:
void _hal_cpuInit(void)
{
    if (hal_started() == 0) {
        nCpusStarted++;
        hal_cpuDataSyncBarrier();
        hal_cpuSignalEvent();
    } else {
        hal_cpuAtomicInc(&nCpusStarted);
        hal_cpuDataSyncBarrier();
        hal_cpuSignalEvent();
    }
    /* NEW: block until all cpus have reached this point. */
    while (hal_cpuAtomicGet(&nCpusStarted) != hal_cpuGetCount()) {
        hal_cpuWaitForEvent();
    }
}
```

This matches the zynqmp pattern at `hal/aarch64/zynqmp/zynqmp.c:528`.
With this synchronization, primary doesn't proceed past
`_hal_cpuInit` (called from `_hal_init`) until all 4 cpus have
arrived. Secondaries' downstream init then runs concurrently
with primary's vm/proc init in a way that's been validated on
zynqmp.

## Status

Session ending with code committed at HEAD = kernel `774d188d`,
plo `0722997`, project `a74eac4` (the marker-stripped clean
state). NOT the experimental `562fb6ad`/`7d69dc7b` because the
experimental state has known bugs (low-PA markers fault after
vbar; concurrent secondary C activity hangs vm_init).
