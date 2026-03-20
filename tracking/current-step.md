# Current Step

## Metadata

- Step ID: `STEP-0034`
- Title: Implement backend-state wait-to-ticks helper
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- add the first backend-state forward-conversion helper for turning relative microseconds into architectural timer ticks

## Scope

In scope:

- update `hal/aarch64/gtimer_backend.h`
- update `hal/aarch64/gtimer_backend.c`
- add a state-based helper for converting relative microseconds to timer ticks
- validate the existing `aarch64a53-zynqmp-qemu` build in `phoenix-dev`

Out of scope:

- adding a new QEMU target
- changing the active timer backend for any target
- implementing the public generic `hal_timer*` entry points
- programming timer or control registers
- implementing timer interrupt registration
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

- the backend-state layer exposes a helper that converts a relative wait time in microseconds to timer ticks
- the helper uses the frequency stored in backend state rather than open-coded call-site math
- the helper stays computational only and does not program timer registers
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
  `manifests/2026-03-20-aarch64-gtimer-wait-to-ticks-scope.md`

## Notes

- Risks:
  the step must stay read-only with respect to timer register programming and must not start arming the timer yet
- Dependencies:
  completed scoping step from `STEP-0033`
- User-visible control point before next step:
  after this step lands, the next slice can target timer-register wrappers or timer-arming policy, but not both at once
