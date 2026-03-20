# Current Step

## Metadata

- Step ID: `STEP-0079`
- Title: Add the generic AArch64 devices target makefile
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- add the minimal `phoenix-rtos-devices` target file needed to unblock generic-target validation before PL011 driver work

## Scope

In scope:

- add `_targets/Makefile.aarch64a53-generic`
- keep the file intentionally minimal and free of PL011 driver policy
- validate `phoenix-rtos-devices` directly on the generic target

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
- `docs/status.md`
- tracking files and manifest updates for this step
- direct generic devices-target build output

## Acceptance Criteria

- `_targets/Makefile.aarch64a53-generic` exists
- `phoenix-rtos-devices` validates directly for `aarch64a53-generic-qemu`
- the change stays repo-local and does not include PL011 driver source yet

## Validation Plan

- Review:
  inspect the new target file against nearby generic target-file patterns
- Build:
  validate `phoenix-rtos-devices` directly for `aarch64a53-generic-qemu` in `phoenix-dev`
- Emulator:
  not applicable
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-generic-devices-target-scope.md`

## Notes

- Risks:
  the result must stay as one repo-local target-file step and must not silently turn into PL011 driver implementation
- Dependencies:
  completed implementation step `STEP-0078`
- User-visible control point before next step:
  after the target file lands, the next step should scope the first PL011 driver slice
