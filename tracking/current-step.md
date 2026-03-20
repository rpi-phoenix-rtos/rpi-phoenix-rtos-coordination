# Current Step

## Metadata

- Step ID: `STEP-0215`
- Title: Implement the Pi 4 GIC CPU-interface pending probe
- Status: `planned`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- add one bounded GIC CPU-interface readback on the direct Pi 4 QEMU timer
  path and compare it with the existing distributor pending evidence

## Scope

In scope:

- add one helper to read the GIC CPU-interface highest-pending register
- extend the existing bounded timer probe with one `GICC_HPPIR` readback
- validate the Pi 4 QEMU lane and use a generic build as a common-code
  guardrail
- update manifests and docs with the result

Out of scope:

- new scheduler or timer-programming changes
- new local-controller experiments
- broad interrupt-controller redesign
- Pi 5 or RP1 work

## Expected Repositories

- `phoenix-rtos-kernel`
- coordination repo

## Expected Files Or Subsystems

- `sources/phoenix-rtos-kernel/hal/aarch64/interrupts_gicv2.c`
- `sources/phoenix-rtos-kernel/hal/aarch64/interrupts_gicv2.h`
- `sources/phoenix-rtos-kernel/hal/aarch64/gtimer_timer.c`
- completed Pi 4 QEMU timer-wiring analysis
- manifests and tracking updates for this implementation step

## Acceptance Criteria

- the Pi 4 lane clearly reports whether `GICC_HPPIR` sees a pending interrupt
  after the timer expires
- the generic build guardrail remains healthy
- the result narrows the next move to one concrete direct GTIMER-to-GIC
  follow-up

## Validation Plan

- Review:
  inspect that the change stays bounded to one CPU-interface probe
- Build:
  - `LIBPHOENIX_DEVEL_MODE=n TARGET=aarch64a53-generic-qemu ./phoenix-rtos-build/build.sh clean host core project image`
  - `LIBPHOENIX_DEVEL_MODE=n RPI4B_DTB_PATH=$HOME/external/raspberrypi-firmware/boot/bcm2711-rpi-4-b.dtb RPI4B_QEMU_MEMORY_SIZE=80000000 TARGET=aarch64a72-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image`
- Emulator:
  - run the Pi 4 A72 `raspi4b` lane and compare:
    - `gtimer: pending`
    - `gtimer: ppi pending`
    - `gtimer: hppir`
    - `gic: timer dispatch`
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-rpi4b-gic-cpu-interface-scope.md`

## Notes

- Risks:
  do not mix this probe with a new timer-source or GIC policy change
- Dependencies:
  completed `STEP-0214` Pi 4 GIC CPU-interface scope
- Source reminder:
  local QEMU `10.2.2` source proves that `raspi4b` wires `GTIMER_PHYS`
  directly to GIC PPI 14 in `hw/arm/bcm2838.c`
- Architecture reminder:
  Raspberry Pi 4 Model B is based on BCM2711 with a quad-core Cortex-A72 CPU;
  treat `aarch64a53-generic-rpi4b` only as a temporary diagnostic lane and
  keep new target work centered on `aarch64a72-generic-rpi4b`
- User-visible control point before next step:
  after this step lands, the next bounded move should depend only on whether
  the CPU interface sees a pending interrupt ID
