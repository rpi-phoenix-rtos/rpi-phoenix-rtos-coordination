# 2026-05-23 — SMP: ALL 4 CPUs reach kernel `_start` (correction)

## Headline

`smp: am=0123 k=0123` — every cpu0/1/2/3 reaches BOTH armstub
`start_late` AND kernel `_start`.

Previous conclusion ("firmware only delivers one secondary, randomly")
was **wrong**. It was based on UART-byte markers, which are
unreliable across 4 cores due to PL011 contention and possible
firmware-side PL011 state issues for the secondary delivery path.

Memory-based markers (per-cpu write to PA 0x40 + cpu_id*4 at
armstub entry, PA 0x50 + cpu_id*4 at kernel `_start`, with cpu0
reading both arrays on its way through kernel `_start` and
printing them via UART) tell a completely different story.

## What that means

The Pi 4 firmware bring-up of secondaries is healthy. The
canonical armstub spin-table protocol works fine on this hardware
+ firmware combo. The Phase D path forward is much shorter than
the previous note suggested.

Specifically: secondaries DO reach kernel `_start`, run through
the shared MMU bring-up code (lines 0-664 of `_init.S`), hit the
`cbnz x8, _other_core_trap` MPIDR check, branch to
`_other_core_trap`, wait at WFE for primary's `nCpusStarted` bump
(SEV from `_hal_cpuInit`), exit the WFE-poll loop, jump to
`_other_core_virtual`. With NUM_CPUS=1 the parker at the top of
`_other_core_virtual` catches them in a DAIF-masked WFI loop —
which is correct single-core behaviour.

With NUM_CPUS=4 they fall through the parker into the C-level
per-CPU init helpers (`_hal_interruptsInitPerCPU` →
`_hal_cpuInit` → `_hal_timerInitPerCPU`) and then into the WFI
loop at the bottom of `_other_core_virtual` that I added under
Phase D.

The bring-up "mystery" was a measurement bug, not a hardware
limitation.

## Action plan, revised

The earlier kernel/plo/armstub Phase D commits (per-CPU stacks,
SEV in `_hal_cpuInit`, banked-PPI enable, per-CPU CNTV arm)
should actually work. The work is preserved on
`agent/rpi4-program-reloc` (see kernel commit `979e05c0` and
the kernel pause commit `be058044` which reverted NUM_CPUS to 1
while we sorted out diagnostics).

Next session, with the BCM2711 PCIe bridge back to baseline (it's
currently stuck in rc=-19), the plan is:

1. Flip NUM_CPUS back to 4 in `hal/aarch64/generic/config.h`.
2. Replace the diagnostic UART markers I stripped (kernel
   `774d188d`, project `833f654`, plo `0722997`) with
   memory-based markers at every per-CPU bring-up step
   (entry of `_hal_interruptsInitPerCPU`, after PPI enable,
   entry of `_hal_cpuInit`, entry of `_hal_timerInitPerCPU`,
   entry of `threads_timeintr`). Dump them via cpu0 at end of
   `main_initthr` after the spawn loop.
3. Validate that secondaries actually fire their per-CPU init
   path and start receiving timer interrupts. Iterate from
   there.

## Code state for this session

- `phoenix-armstub8-rpi4.S` (project): start_late writes
  0x10000000 + cpu_id to PA 0x40 + cpu_id*4 at entry.
- `_init.S` (kernel): `_start` writes 0x20000000 + cpu_id to
  PA 0x50 + cpu_id*4 at entry (every cpu, unconditional).
  cpu0 then prints `smp: am=NNNN k=NNNN` via UART (the only
  consumer of the markers; secondaries don't print).

The boot UART output now opens with one line like:
  `smp: am=0123 k=0123`

NUM_CPUS is still 1; secondaries reach kernel `_start`, write
their k marker, then go through `cbnz x8, _other_core_trap`,
secondary_spin → trap, exit on `nCpusStarted!=0`, hit the
NUM_CPUS==1 parker at top of `_other_core_virtual`, and park
forever. Boot reaches `(psh)%` normally; primary is unaffected.

The markers themselves are diagnostic-only and should come out
once Phase D is validated. They're useful for the next session
so I'm leaving them in.
