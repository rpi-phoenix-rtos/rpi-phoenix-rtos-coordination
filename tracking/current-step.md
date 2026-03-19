# Current Step

## Metadata

- Step ID: `STEP-0015`
- Title: Move AArch64 timer IRQ knowledge behind the timer HAL
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- remove the hard compile-time timer IRQ dependency from the common AArch64 GICv2 code by moving timer IRQ knowledge behind the timer HAL API, while preserving the current `aarch64a53-zynqmp` behavior

## Scope

In scope:

- update `phoenix-rtos-kernel/hal/timer.h`
- update `phoenix-rtos-kernel/hal/aarch64/interrupts_gicv2.c`
- update `phoenix-rtos-kernel/hal/aarch64/zynqmp/timer.c`
- add a timer IRQ query to the timer HAL API
- switch the common AArch64 GICv2 trace-suppression logic to use that timer HAL query instead of the `TIMER_IRQ_ID` macro
- preserve the current ZynqMP runtime behavior and build results

Out of scope:

- adding a new QEMU target
- adding generic ARM timer runtime code
- adding PL011 console code
- Raspberry Pi-specific code

## Expected Repositories

- `phoenix-rtos-kernel`
- coordination repo

## Expected Files Or Subsystems

- `sources/phoenix-rtos-kernel/hal/timer.h`
- `sources/phoenix-rtos-kernel/hal/aarch64/interrupts_gicv2.c`
- `sources/phoenix-rtos-kernel/hal/aarch64/zynqmp/timer.c`
- copied-buildroot AArch64 validation workflow
- tracking files and manifest updates after validation

## Acceptance Criteria

- the common AArch64 GICv2 code no longer depends directly on `TIMER_IRQ_ID`
- the timer HAL exposes an IRQ query, and the current ZynqMP AArch64 timer backend implements it
- `TARGET=aarch64a53-zynqmp-qemu ./phoenix-rtos-build/build.sh clean host core project` still succeeds inside `phoenix-dev` using the copied buildroot

## Validation Plan

- Build:
  refresh the copied buildroot, then run the existing `aarch64a53-zynqmp-qemu` build path in `phoenix-dev`
- Emulator:
  not applicable
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-runtime-step-scope.md`

## Notes

- Risks:
  this is a preparatory runtime step, not the generic ARM timer implementation itself
- Dependencies:
  completed DTB preparation series and the runtime-step scope decision from `STEP-0014`
- User-visible control point before next step:
  present the exact timer HAL API change, validation command, and resulting commit before moving into generic timer runtime code
