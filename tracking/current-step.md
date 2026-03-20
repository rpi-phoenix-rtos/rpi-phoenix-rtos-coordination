# Current Step

## Metadata

- Step ID: `STEP-0028`
- Title: Implement common AArch64 `gtimer` helper layer
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- add a compiled common AArch64 `gtimer` helper layer that hides the physical-versus-virtual timer sysreg split from future backend code while preserving current runtime behavior

## Scope

In scope:

- add `hal/aarch64/gtimer.h`
- add `hal/aarch64/gtimer.c`
- compile the helper in the current common AArch64 build
- preserve the existing ZynqMP runtime path and validate the existing `aarch64a53-zynqmp-qemu` build in `phoenix-dev`

Out of scope:

- adding a new QEMU target
- changing the timer backend implementation itself
- implementing the common generic timer runtime backend itself
- adding PL011 console code
- Raspberry Pi-specific code

## Expected Repositories

- `phoenix-rtos-kernel`
- coordination repo

## Expected Files Or Subsystems

- `hal/aarch64/Makefile`
- `hal/aarch64/gtimer.h`
- `hal/aarch64/gtimer.c`
- tracking files and manifest updates for this step

## Acceptance Criteria

- the common AArch64 build now compiles a `gtimer` helper layer
- the helper layer exposes source-keyed access for counter reads, control reads and writes, timer programming, and source naming
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
  `manifests/2026-03-20-aarch64-gtimer-helper-step-scope.md`

## Notes

- Risks:
  the step must remain helper-only and must not introduce duplicate public timer-backend policy
- Dependencies:
  completed scoping step from `STEP-0027`
- User-visible control point before next step:
  after this helper layer lands, the next step should either start a generic backend skeleton on top of it or stop for re-scoping
