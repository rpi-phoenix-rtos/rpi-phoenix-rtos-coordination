# Current Step

## Metadata

- Step ID: `STEP-0092`
- Title: Define the first console-ready diagnostic step after `pl011-tty: started`
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- identify the smallest next diagnostic that can prove whether `pl011-tty` reaches `/dev/console` readiness after startup

## Scope

In scope:

- inspect the new smoke result that includes `pl011-tty: started`
- choose the smallest follow-up diagnostic that distinguishes “driver started” from “console fully ready”
- stop before implementing that diagnostic

Out of scope:

- all upstream source changes
- Pi 4 board-specific code
- Raspberry Pi-specific code
- `phoenix-rtos-tests` target additions

## Expected Repositories

- coordination repo
- `phoenix-rtos-devices`

## Expected Files Or Subsystems

- `phoenix-rtos-project/_targets/aarch64a53/generic/user.plo.yaml`
- comparable QEMU `user.plo` files
- `phoenix-rtos-devices/tty/pl011-tty/*`
- `docs/status.md`
- tracking files and manifest updates for this step
- generic QEMU smoke output
- generic utils packaging expectations

## Acceptance Criteria

- the next diagnostic step is selected from the new smoke evidence
- the follow-up stays as one small implementation commit where possible
- the selected step advances the generic QEMU fast lane directly

## Validation Plan

- Review:
  inspect the new smoke boundary and keep the selected follow-up diagnostic minimal
- Build:
  use the current runtime evidence and nearby driver code to choose the smallest useful follow-up
- Emulator:
  not applicable
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-generic-userspace-diagnostic.md`

## Notes

- Risks:
  the result must stay as a localized diagnostic-planning step and must not silently turn into broader console-driver refactoring
- Dependencies:
  completed implementation step `STEP-0091`
- User-visible control point before next step:
  after the next diagnostic step is selected, the follow-up implementation should stay narrow and validation-driven
