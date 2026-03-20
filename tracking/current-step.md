# Current Step

## Metadata

- Step ID: `STEP-0097`
- Title: Add a short generic `user.plo` wait before `pl011-tty`
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- add the smallest startup-timing change that can test whether `pl011-tty` is starting before the generic `/dev` namespace is ready

## Scope

In scope:

- update `phoenix-rtos-project/_targets/aarch64a53/generic/user.plo.yaml`
- insert one short `wait` after `dummyfs;-N;devfs;-D` and before `pl011-tty`
- rebuild the needed generic artifacts and rerun the generic QEMU smoke lane

Out of scope:

- driver refactoring
- multiple wait entries or larger boot-script reshaping
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

- the generic `user.plo` adds one short wait only between `dummyfs` and `pl011-tty`
- the needed artifacts are rebuilt and repackaged
- the generic QEMU smoke lane is rerun from the refreshed image

## Validation Plan

- Review:
  inspect the script change and keep it limited to one startup-timing test
- Build:
  rebuild the generic `host project image` lane in `phoenix-dev`
- Emulator:
  rerun `timeout 12s ./scripts/aarch64a53-generic-qemu.sh`
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-generic-devfs-wait-scope.md`

## Notes

- Risks:
  the result must stay as one localized timing test and must not silently turn into broader boot-script experimentation
- Dependencies:
  completed implementation step `STEP-0096`
- User-visible control point before next step:
  after the timing test lands, the next follow-up should be chosen from the new smoke output
