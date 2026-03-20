# Current Step

## Metadata

- Step ID: `STEP-0040`
- Title: Implement backend-state IRQ-ownership helpers
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- add backend-state helpers for querying the selected timer IRQ and registering a handler against it

## Scope

In scope:

- update `hal/aarch64/gtimer_backend.h`
- update `hal/aarch64/gtimer_backend.c`
- add a state-based helper that returns the selected timer IRQ
- add a state-based helper that prepares and registers an interrupt handler for that IRQ
- validate the existing `aarch64a53-zynqmp-qemu` build in `phoenix-dev`

Out of scope:

- adding a new QEMU target
- changing the active timer backend for any target
- implementing the public generic `hal_timer*` entry points
- adding disable or cancellation semantics
- switching any target to the common AArch64 timer backend
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

- the backend-state layer exposes a helper that returns the selected timer IRQ from backend state
- the backend-state layer exposes a helper that prepares and registers a handler on that IRQ
- the helpers avoid open-coded `state->irq` plumbing at later call sites
- the helpers do not introduce public `hal_timer*` integration or switch the active backend
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
  `manifests/2026-03-20-aarch64-gtimer-irq-ownership-scope.md`

## Notes

- Risks:
  the step must stay backend-local and must not also take over the public timer HAL or switch targets to the common backend
- Dependencies:
  completed scoping step from `STEP-0039`
- User-visible control point before next step:
  after this step lands, the next slice can target public timer-HAL wiring or backend selection, but not both at once
