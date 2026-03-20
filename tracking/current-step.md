# Current Step

## Metadata

- Step ID: `STEP-0086`
- Title: Define the first generic `user.plo` integration step for `dummyfs` and `pl011-tty`
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- identify the smallest `user.plo` change that lets the generic QEMU image bring up `dummyfs` before `pl011-tty`

## Scope

In scope:

- inspect current generic and comparable QEMU `user.plo` sequences
- choose the smallest ordering and component set that can make `/dev/console` creation viable
- stop before editing `user.plo`

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
- direct script references and, if needed, runtime evidence

## Acceptance Criteria

- the smallest `user.plo` integration step is selected from actual script patterns
- the follow-up stays as one small implementation commit where possible
- the selected step advances the generic QEMU fast lane directly

## Validation Plan

- Review:
  inspect current generic and comparable QEMU `user.plo` scripts and keep the selected sequence minimal
- Build:
  use direct build or runtime evidence only as needed to choose the smallest useful `user.plo` step
- Emulator:
  not applicable
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-pl011-board-config.md`

## Notes

- Risks:
  the result must stay as a `user.plo` planning step and must not silently turn into generic shell bring-up in one jump
- Dependencies:
  completed implementation step `STEP-0085`
- User-visible control point before next step:
  after the `user.plo` step is selected, the follow-up implementation should stay narrow and validation-driven
