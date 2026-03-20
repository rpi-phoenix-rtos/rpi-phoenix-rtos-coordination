# Current Step

## Metadata

- Step ID: `STEP-0084`
- Title: Define the first generic PL011 board-config wiring step
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- identify the smallest board-config change needed to make `pl011-tty` usable on the generic QEMU target

## Scope

In scope:

- inspect the current generic QEMU `board_config.h`
- inspect the `pl011-tty` board-config contract
- choose the smallest cross-repo wiring step that gives the driver real runtime parameters

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
- direct code references and, if needed, direct build evidence

## Acceptance Criteria

- the smallest board-config step is selected from the current generic-QEMU project contract
- the follow-up remains one small implementation commit where possible
- the selected step advances the generic QEMU fast lane directly

## Validation Plan

- Review:
  inspect the current generic QEMU board config against the `pl011-tty` contract
- Build:
  use direct build evidence only as needed to confirm the smallest useful board-config step
- Emulator:
  not applicable
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-pl011-target-integration.md`

## Notes

- Risks:
  the result must stay as a narrow board-config planning step and must not silently turn into `user.plo` integration or smoke-lane debugging
- Dependencies:
  completed implementation step `STEP-0083`
- User-visible control point before next step:
  after the board-config step is selected, the follow-up implementation should stay narrow and validation-driven
