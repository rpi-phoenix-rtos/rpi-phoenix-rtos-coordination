# 2026-05-23 — SMP bring-up: firmware only delivers ONE secondary to armstub

## Diagnostic

Added a unique 3-byte marker `;X<digit>` at the very first
instruction of `start_late` in `phoenix-armstub8-rpi4.S` (commit
to project repo this session). Single boot cycle observation:

  - `;X3` fired exactly once (cpu3 reached armstub start_late)
  - `;X0`, `;X1`, `;X2` did not appear

This is the cleanest signal yet: the Pi 4 firmware on the current
stack:

  - **Does NOT deliver cpu0 to armstub at all.** It loads
    kernel8.img at PA 0x80000 and starts cpu0 there directly. Our
    kernel8.img is the relocator+plo bundle, so cpu0 enters at
    the relocator's `_start` and never touches PA 0.
  - **Delivers exactly ONE of cpu1/2/3 to armstub start_late, and
    only once.** Across all earlier markered tests (the
    `<Z<digit>` per-CPU markers in `in_el2`), this single
    secondary rotated non-deterministically between cpu1 and cpu3
    (cpu2 never seen reaching armstub in any run).
  - **Does NOT bring up the other two secondaries.** They stay
    powered-down (or in some pre-armstub WFI managed by the
    firmware-level secure monitor).

Two strong implications:

1. The armstub's `secondary_spin` loop is dead code on this Pi 4
   firmware version. cpu0 doesn't use it (cpu0 isn't in armstub).
   The one secondary that DOES reach armstub goes into
   `secondary_spin` and waits for an OS-driven write to
   `spin_cpu<N>` at PA 0xd8 + N*8. The two missing secondaries
   never get there at all.

2. To enable SMP on Pi 4 with this firmware, **we have to wake
   cpu1/2/3 ourselves from the kernel side**, using either the
   BCM2711 ARM_LOCAL MAILBOX SET registers or a PSCI CPU_ON `smc`
   call via a secure monitor we add to the armstub. The
   spin-table protocol as it stands is only useful for the one
   secondary firmware happens to deliver.

## Why this matters

The Phase D work on the kernel side (per-CPU stacks, SEV, banked
PPI enable, etc.) was all correct in isolation — it would have
made secondaries run if firmware had delivered them to the
kernel. The actual gap is wake mechanism, not anything
downstream.

## Next experiment plan

Implement **BCM2711 ARM_LOCAL MAILBOX SET poke** as the wake
mechanism. The BCM2711 ARM_LOCAL block lives at PA 0x4c000000;
per-core wake registers are at:

  - `MBOX_SET[N]` at 0x4c000080 + N*0x10
  - `MBOX_CLEAR[N]` at 0x4c0000c0 + N*0x10

Writing a non-zero value to `MBOX_SET[N]` for core N causes the
core (if powered down, the BCM2711 power-controller wakes it) to
exit its WFI state and branch to the address held in the mailbox.
This is the same mechanism Linux's bcm2836_smp_boot_secondary
uses on BCM2836/2837 and that Circle / bare-metal Pi 4 stacks use
on BCM2711.

Sketch:

  1. Define a small kernel-side entry stub (`_smp_secondary_entry`)
     at a known PA. It sets up MMU using primary's TTBR1, then
     branches to `_other_core_virtual`.
  2. From the primary's `_hal_cpuInit` (or a new helper), iterate
     over cpu_id 1..3:
       - mmap the ARM_LOCAL MBOX_SET register for that core.
       - Write `&_smp_secondary_entry` to it.
       - `dsb sy; sev`.
  3. Wait for the secondary's `_hal_cpuInit` to increment
     `nCpusStarted`. Time out after N ms; report which secondaries
     came up.

This needs to be tested AFTER the BCM2711 PCIe bridge state is
back to a clean baseline (currently stuck in rc=-19 poison).

## Status

Diagnostic committed (single-byte change in project repo). Marker
should stay in place for the next session to confirm the
single-secondary-per-boot pattern repeats — if cpu2 ever fires
`;X2`, we'd know the secondary brought up by firmware really is
random. Three-cycle data from earlier (before marker stripping):
cpu3, none, cpu1. Adding today's cpu3: 4 datapoints, distribution
{cpu1: 1, cpu2: 0, cpu3: 2, none: 1}. Consistent with "firmware
brings up at most one secondary, biased toward cpu3."

Code change summary: armstub `start_late` opens with the ";X<digit>"
marker block. No kernel/plo changes this iteration.
