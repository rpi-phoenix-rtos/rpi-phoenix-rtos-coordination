# Current Step

## Metadata

- Step ID: `STEP-0085`
- Title: Wire generic QEMU PL011 values into `board_config.h`
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- give the generic QEMU project board config the PL011 base and clock values needed by `pl011-tty`

## Scope

In scope:

- update `_projects/aarch64a53-generic-qemu/board_config.h`
- keep the change limited to the PL011 driver contract
- validate the generic devices build with the populated board config

Out of scope:

- all upstream source changes
- Pi 4 board-specific code
- Raspberry Pi-specific code
- `phoenix-rtos-tests` target additions

## Expected Repositories

- coordination repo
- `phoenix-rtos-devices`

## Expected Files Or Subsystems

- `phoenix-rtos-project/_projects/aarch64a53-generic-qemu/board_config.h`
- `phoenix-rtos-devices/tty/pl011-tty/*`
- `docs/status.md`
- tracking files and manifest updates for this step
- direct generic devices build output

## Acceptance Criteria

- the generic QEMU board config now defines the PL011 base and clock expected by `pl011-tty`
- the generic devices build remains green with the populated board config
- the change stays narrow and does not touch `user.plo`

## Validation Plan

- Review:
  inspect the board-config change against the `pl011-tty` contract and keep it minimal
- Build:
  validate `phoenix-rtos-devices all` directly for `aarch64a53-generic-qemu` in `phoenix-dev`
- Emulator:
  not applicable
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-pl011-board-config-scope.md`

## Notes

- Risks:
  the result must stay as one narrow board-config step and must not silently turn into `user.plo` integration or smoke-lane debugging
- Dependencies:
  completed implementation step `STEP-0084`
- User-visible control point before next step:
  after the board-config wiring lands, the next step should scope the first runtime image integration of `pl011-tty`
