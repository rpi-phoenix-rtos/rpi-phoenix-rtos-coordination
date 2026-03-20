# Current Step

## Metadata

- Step ID: `STEP-0038`
- Title: Implement backend-state timer-arming helper
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- add the first backend-state helper that arms the selected architectural timer for a relative wakeup

## Scope

In scope:

- update `hal/aarch64/gtimer_backend.h`
- update `hal/aarch64/gtimer_backend.c`
- add a state-based helper that converts a relative wait to timer ticks
- clamp positive waits away from zero programmed ticks
- program the selected timer and enable it unmasked through the backend wrappers
- validate the existing `aarch64a53-zynqmp-qemu` build in `phoenix-dev`

Out of scope:

- adding a new QEMU target
- changing the active timer backend for any target
- implementing the public generic `hal_timer*` entry points
- implementing timer interrupt registration
- adding disable or cancellation semantics
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

- the backend-state layer exposes a helper that arms the selected timer for a relative wait in microseconds
- positive waits are not programmed as zero timer ticks
- the helper enables the selected timer unmasked through the backend wrappers
- the helper does not introduce public `hal_timer*` integration or IRQ registration
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
  `manifests/2026-03-20-aarch64-gtimer-arming-scope.md`

## Notes

- Risks:
  the step must not grow into public HAL takeover or IRQ registration; it should stay as a backend-local arming helper only
- Dependencies:
  completed scoping step from `STEP-0037`
- User-visible control point before next step:
  after this step lands, the next slice can target IRQ registration or public timer-HAL wiring, but not both at once
