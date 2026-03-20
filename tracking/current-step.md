# Current Step

## Metadata

- Step ID: `STEP-0082`
- Title: Define the first generic PL011 target-integration step
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- identify the smallest generic-target integration step for the new `pl011-tty` driver

## Scope

In scope:

- inspect the current generic devices target and build flow
- confirm whether the next smallest step is target default-component integration, board-config population, or broader core-lane validation
- select the narrowest useful follow-up

Out of scope:

- all upstream source changes
- Pi 4 board-specific code
- Raspberry Pi-specific code
- `phoenix-rtos-tests` target additions

## Expected Repositories

- coordination repo
- `phoenix-rtos-devices`

## Expected Files Or Subsystems

- `phoenix-rtos-devices/_targets/*`
- `phoenix-rtos-devices/tty/*`
- `phoenix-rtos-devices/tty/pl011-tty/*`
- `docs/status.md`
- tracking files and manifest updates for this step
- direct build or target-selection evidence if needed

## Acceptance Criteria

- the next step is selected from actual generic-target integration constraints
- the follow-up remains one small commit where possible
- the selected step advances the generic QEMU fast lane rather than unrelated cleanup

## Validation Plan

- Review:
  inspect the new driver and generic target file together and keep the selected integration slice minimal
- Build:
  use direct repo or broader generic build evidence only as needed to choose the next smallest integration step
- Emulator:
  not applicable
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-pl011-tty.md`

## Notes

- Risks:
  the result must stay as an integration-planning step and must not silently turn into multi-repo runtime bring-up
- Dependencies:
  completed implementation step `STEP-0081`
- User-visible control point before next step:
  after the integration step is selected, the follow-up implementation should stay narrow and validation-driven
