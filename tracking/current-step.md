# Current Step

## Metadata

- Step ID: `STEP-0213`
- Title: Restore the Pi 4 QEMU fast lane after the local-controller detour
- Status: `planned`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- remove the now-proven-irrelevant local-controller experiment hooks from the
  Pi 4 QEMU fast lane so the next direct GTIMER-to-GIC follow-up starts from a
  clean baseline again

## Scope

In scope:

- remove the Pi 4-only local base and local prescaler board hooks
- remove the temporary local-controller route and local-pending diagnostics
  from the active kernel path if they are only serving the detour
- validate that the Pi 4 QEMU lane returns to the pre-local-controller
  evidence set
- update manifests and docs with the restored QEMU-specific baseline

Out of scope:

- new GTIMER-to-GIC experiments beyond restoring the baseline
- scheduler or VM changes
- broad interrupt-controller redesign
- Pi 5 or RP1 work

## Expected Repositories

- `phoenix-rtos-kernel`
- `phoenix-rtos-project`
- coordination repo

## Expected Files Or Subsystems

- `sources/phoenix-rtos-kernel/hal/aarch64/interrupts_gicv2.c`
- `sources/phoenix-rtos-kernel/hal/aarch64/gtimer_timer.c`
- `sources/phoenix-rtos-kernel/hal/aarch64/generic/config.h`
- `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/board_config.h`
- completed Pi 4 QEMU timer-wiring analysis
- manifests and tracking updates for this implementation step

## Acceptance Criteria

- the Pi 4 QEMU lane returns to the pre-detour evidence set
- the generic guardrail remains healthy if common code is touched
- the next direct GTIMER-to-GIC experiment can start from a clean QEMU
  baseline

## Validation Plan

- Review:
  inspect that the revert removes only the local-controller detour
- Build:
  - `LIBPHOENIX_DEVEL_MODE=n TARGET=aarch64a53-generic-qemu ./phoenix-rtos-build/build.sh clean host core project image`
  - `LIBPHOENIX_DEVEL_MODE=n RPI4B_DTB_PATH=$HOME/external/raspberrypi-firmware/boot/bcm2711-rpi-4-b.dtb RPI4B_QEMU_MEMORY_SIZE=80000000 TARGET=aarch64a72-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image`
- Emulator:
  - run the Pi 4 A72 `raspi4b` lane and compare:
    - `gic: timer handler set`
    - `gtimer: pending`
    - `gic: timer dispatch`
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-rpi4b-qemu-timer-wiring-analysis.md`

## Notes

- Risks:
  do not mix the baseline restore with the next direct GTIMER-to-GIC
  experiment
- Dependencies:
  completed `STEP-0212` Pi 4 QEMU timer-wiring analysis
- Source reminder:
  local QEMU `10.2.2` source proves that `raspi4b` wires `GTIMER_PHYS`
  directly to GIC PPI 14 in `hw/arm/bcm2838.c`, so the local controller detour
  is not on the active Pi 4 QEMU timer path
- Architecture reminder:
  Raspberry Pi 4 Model B is based on BCM2711 with a quad-core Cortex-A72 CPU;
  treat `aarch64a53-generic-rpi4b` only as a temporary diagnostic lane and
  keep new target work centered on `aarch64a72-generic-rpi4b`
- User-visible control point before next step:
  after this step lands, the next bounded move should operate only on the
  direct GTIMER-to-GIC path seen in QEMU `bcm2838.c`
