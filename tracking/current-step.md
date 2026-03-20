# Current Step

## Metadata

- Step ID: `STEP-0098`
- Title: Define the first Pi 4-specific no-hardware scaffold step after the generic QEMU runtime boundary
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- identify the smallest Pi 4-oriented implementation step that can start real board scaffolding without depending on real hardware and without losing the generic QEMU lane

## Scope

In scope:

- inspect the current generic-QEMU boundary together with the Pi 4 platform notes
- choose the smallest Pi 4-specific scaffold change that can be validated without real hardware
- stop before implementing that scaffold change

Out of scope:

- broad multi-repo Pi 4 bring-up
- real-hardware-only validation
- Pi 5 or RP1 work
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

- the next Pi 4-oriented step is selected from current evidence rather than from broad wishlisting
- the follow-up stays as one small implementation commit where possible
- the selected step directly improves the path to a first Pi 4 UART/kernel boot

## Validation Plan

- Review:
  inspect the generic runtime boundary and Pi 4 bring-up notes together
- Build:
  use local source and project documentation to choose the smallest useful Pi 4 scaffold step
- Emulator:
  not applicable
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-generic-wait-test.md`

## Notes

- Risks:
  the result must stay as a localized planning step and must not silently widen into a broad Pi 4 porting batch
- Dependencies:
  abandoned implementation experiment `STEP-0097`
- User-visible control point before next step:
  after the next Pi 4-oriented step is selected, implementation should stay narrowly scoped and build-validated
