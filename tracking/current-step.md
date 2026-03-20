# Current Step

## Metadata

- Step ID: `STEP-0091`
- Title: Add a direct PL011 startup banner to `pl011-tty`
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- add the smallest direct userspace-start diagnostic that can prove whether the generic runtime path reaches `pl011-tty`

## Scope

In scope:

- update `phoenix-rtos-devices/tty/pl011-tty/pl011-tty.c`
- emit a raw PL011 banner after the UART mapping and configuration succeed
- rebuild the needed generic artifacts and rerun the generic QEMU smoke lane

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

- `pl011-tty` emits a raw startup banner on the generic QEMU path
- the needed artifacts are rebuilt and repackaged
- the generic QEMU smoke lane is rerun from the refreshed image

## Validation Plan

- Review:
  inspect the `pl011-tty` diagnostic change and keep it minimal and localized
- Build:
  rebuild the needed generic artifacts in `phoenix-dev`
- Emulator:
  not applicable
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-generic-userspace-diagnostic-scope.md`

## Notes

- Risks:
  the result must stay as one localized diagnostic step and must not silently turn into broader console-driver refactoring
- Dependencies:
  completed implementation step `STEP-0090`
- User-visible control point before next step:
  after the diagnostic step lands, the next step should be chosen from the new smoke output
