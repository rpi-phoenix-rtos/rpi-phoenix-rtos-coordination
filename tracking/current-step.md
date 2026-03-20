# Current Step

## Metadata

- Step ID: `STEP-0096`
- Title: Define the first generic startup-timing test after the missing `/dev/tty0` banner
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- identify the smallest non-hardware runtime step that can test whether `pl011-tty` is starting before the generic `/dev` namespace is ready

## Scope

In scope:

- inspect the updated smoke evidence where neither `/dev/tty0` nor `_PATH_CONSOLE` success banners appear
- use the local `create_dev()` and `dummyfs -D` behavior to choose the smallest startup-timing test
- stop before implementing that test

Out of scope:

- all upstream source changes
- broad driver refactoring
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

- the next runtime step is selected from the current create-dev boundary
- the follow-up stays as one small implementation commit where possible
- the selected step is more likely to move the generic QEMU lane forward than another equally blind diagnostic

## Validation Plan

- Review:
  inspect the current runtime evidence together with `create_dev()` and `dummyfs` startup behavior
- Build:
  use the local source evidence to choose the smallest useful follow-up
- Emulator:
  not applicable
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-generic-tty0-diagnostic.md`

## Notes

- Risks:
  the result must stay as a localized planning step and must not silently turn into speculative broad runtime changes
- Dependencies:
  completed implementation step `STEP-0095`
- User-visible control point before next step:
  after the next test is selected, the implementation should stay narrow and directly tied to the observed startup boundary
