# Current Step

## Metadata

- Step ID: `STEP-0022`
- Title: Add AArch64 timer-backend selection scaffolding
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- make the AArch64 kernel build able to select the timer backend explicitly without changing current runtime behavior, so the eventual common generic timer backend has a clean insertion point

## Scope

In scope:

- add an explicit timer-backend object hook in the common AArch64 kernel Makefile
- move current ZynqMP timer object selection behind that hook
- preserve the existing ZynqMP runtime path and current build output
- validate the existing `aarch64a53-zynqmp-qemu` build in `phoenix-dev`

Out of scope:

- adding a new QEMU target
- implementing the common generic timer runtime backend itself
- changing timer IRQ or wakeup semantics
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

- the common AArch64 kernel Makefile exposes an explicit timer-backend object hook
- the current ZynqMP timer backend is still selected through that hook
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
  `manifests/2026-03-20-aarch64-direct-timer-backend-step-scope.md`

## Notes

- Risks:
  the step must not accidentally introduce runtime behavior changes while only restructuring the build selection path
- Dependencies:
  completed backend-scoping step from `STEP-0021`
- User-visible control point before next step:
  after this scaffold lands, the next step should target either a narrow timer-path correctness fix or the first small piece of common backend logic, but not both at once
