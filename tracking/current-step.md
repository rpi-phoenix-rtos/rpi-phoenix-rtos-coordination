# Current Step

## Metadata

- Step ID: `STEP-0042`
- Title: Implement explicit AArch64 public timer hook
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- split the AArch64 timer build glue so the public timer implementation object is selected explicitly without changing current runtime behavior

## Scope

In scope:

- update `hal/aarch64/Makefile`
- update `hal/aarch64/zynqmp/Makefile`
- introduce a dedicated hook for the public timer implementation object
- keep the current ZynqMP timer implementation selected through the new hook
- validate the existing `aarch64a53-zynqmp-qemu` build in `phoenix-dev`

Out of scope:

- adding a new QEMU target
- changing the selected runtime timer implementation for any target
- implementing the common public `hal_timer*` wrapper file itself
- changing timer runtime behavior
- adding PL011 console code
- Raspberry Pi-specific code

## Expected Repositories

- `phoenix-rtos-kernel`
- coordination repo

## Expected Files Or Subsystems

- `hal/aarch64/gtimer_backend.h`
- `hal/aarch64/gtimer_backend.c`
- tracking files and manifest updates for this step

## Acceptance Criteria

- the AArch64 build exposes a dedicated hook for the public timer implementation object
- the current ZynqMP timer implementation is still selected through that hook
- the step does not change timer runtime behavior or switch any target to a common timer implementation
- the existing `aarch64a53-zynqmp-qemu` build still succeeds in `phoenix-dev`

## Validation Plan

- Build:
  refresh the copied buildroot and rebuild `TARGET=aarch64a53-zynqmp-qemu` with `./phoenix-rtos-build/build.sh clean host core project`
- Emulator:
  not applicable
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-public-timer-hook-scope.md`

## Notes

- Risks:
  the step must stay build-structure-only and must not also add the common public timer implementation file
- Dependencies:
  completed scoping step from `STEP-0041`
- User-visible control point before next step:
  after this step lands, the next slice can target the first common public timer-HAL wrapper file without reopening build glue
