# Current Step

## Metadata

- Step ID: `STEP-0044`
- Title: Implement AArch64 timer-implementation override hook
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- add an explicit AArch64 timer-implementation override hook while keeping the current ZynqMP timer selected by default

## Scope

In scope:

- update `hal/aarch64/Makefile`
- update `hal/aarch64/zynqmp/Makefile`
- add an explicit override path for the selected public timer implementation object
- preserve the current default selection for `aarch64a53-zynqmp-qemu`
- validate the existing `aarch64a53-zynqmp-qemu` build in `phoenix-dev`

Out of scope:

- adding a new QEMU target
- implementing the common public `hal_timer*` wrapper file itself
- changing the default runtime timer implementation for any target
- adding PL011 console code
- Raspberry Pi-specific code

## Expected Repositories

- `phoenix-rtos-kernel`
- coordination repo

## Expected Files Or Subsystems

- `hal/aarch64/Makefile`
- `hal/aarch64/zynqmp/Makefile`
- tracking files and manifest updates for this step

## Acceptance Criteria

- the AArch64 build exposes an explicit override path for the public timer implementation object
- the current ZynqMP timer implementation remains the default selection
- the step does not change default runtime timer behavior
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
  `manifests/2026-03-20-aarch64-public-timer-wrapper-scope.md`

## Notes

- Risks:
  the step must stay build-glue-only and must not also introduce the common public timer file itself
- Dependencies:
  completed scoping step `STEP-0043`
- User-visible control point before next step:
  after this step lands, the next slice can add the first public common timer file without reopening the validation strategy
