# Current Step

## Metadata

- Step ID: `STEP-0075`
- Title: Make `libphoenix` AArch64 reboot support generic
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- apply the next smallest generic AArch64 source change that removes a hard build blocker without pulling in board-device policy

## Scope

In scope:

- remove the unused ZynqMP-only include guard and generic-target `#error` from `libphoenix/arch/aarch64/reboot.c`
- validate `libphoenix` directly for `aarch64a53-generic-qemu` in `phoenix-dev`
- stop before changing any other repo

Out of scope:

- changes in `phoenix-rtos-devices`
- Pi 4 board-specific code
- Raspberry Pi-specific code
- `phoenix-rtos-tests` target additions

## Expected Repositories

- coordination repo
- `phoenix-rtos-project`
- `plo`

## Expected Files Or Subsystems

- `libphoenix/arch/aarch64/reboot.c`
- `docs/status.md`
- tracking files and manifest updates for this step
- direct generic-target build output from `libphoenix`

## Acceptance Criteria

- `libphoenix` no longer hard-errors on non-ZynqMP AArch64 targets
- `libphoenix` validates directly on `aarch64a53-generic-qemu`
- the result records the next smallest remaining generic-lane blocker

## Validation Plan

- Review:
  inspect `libphoenix/arch/aarch64/reboot.c` and keep the change minimal
- Build:
  validate `libphoenix` directly for `aarch64a53-generic-qemu` in `phoenix-dev`
- Emulator:
  not applicable
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-generic-libphoenix-reboot-scope.md`

## Notes

- Risks:
  the result must stay as one repo-local unblock step and must not silently turn into multi-repo implementation work
- Dependencies:
  completed implementation step `STEP-0074`
- User-visible control point before next step:
  after this repo-local unblock lands, the next slice should be the next smallest remaining generic-lane blocker
