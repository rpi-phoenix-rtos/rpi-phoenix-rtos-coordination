# Current Step

## Metadata

- Step ID: `STEP-0074`
- Title: Define smallest generic AArch64 `libphoenix` reboot-support step
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- define the next smallest generic AArch64 source change that removes a hard build blocker without pulling in board-device policy

## Scope

In scope:

- inspect `libphoenix/arch/aarch64/reboot.c`
- determine whether the current non-ZynqMP `#error` can be removed cleanly for the generic target
- choose the narrowest safe `libphoenix` change
- keep the step planning-only

Out of scope:

- implementation code in this planning step
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
- `libphoenix` blocker analysis

## Acceptance Criteria

- the result names the smallest safe `libphoenix` change to apply next
- the result explains why that change is preferred over defining a generic device target first
- the step remains planning-only

## Validation Plan

- Review:
  inspect `libphoenix/arch/aarch64/reboot.c` and the generic-target build blockers
- Build:
  not applicable
- Emulator:
  not applicable
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-generic-utils-target.md`

## Notes

- Risks:
  the result must stay as one repo-local unblock step and must not silently turn into multi-repo implementation work
- Dependencies:
  completed implementation step `STEP-0073`
- User-visible control point before next step:
  after this planning step lands, the next slice should be the selected `libphoenix` change
