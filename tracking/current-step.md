# Current Step

## Metadata

- Step ID: `STEP-0430`
- Title: Scope the first post-armstub Pi 4 visible milestone
- Status: `in_progress`
- Date: `2026-04-08`
- Milestone / phase: `Phase 1`

## Objective

- choose and implement the smallest next no-UART, no-HDMI-independent visible
  proof after the now-confirmed custom Pi 4 armstub entry

## Scope

In scope:

- classifying the latest Pi 4 board result:
  - ACT LED on and steady
  - blank screen
  - no keyboard-visible input effect
- selecting the smallest next hardware-visible milestone later than armstub
- preparing the next code step around early `plo` entry rather than earlier
  firmware media or armstub reachability

Out of scope:

- wide boot redesigns
- unrelated USB, PCIe, or runtime shell work

## Expected Repositories

- coordination repo
- likely `plo`
- possibly `phoenix-rtos-project`

## Expected Files Or Subsystems

- `tracking/current-step.md`
- `tracking/step-history.md`
- `docs/status.md`
- `docs/pi4-first-hardware-trial.md`
- `phoenix-armstub8-rpi4.S`
- `plo/hal/aarch64/generic/_init.S`
- `plo/hal/aarch64/generic/hal.c`

## Acceptance Criteria

- the earlier board result is explicitly classified as post-armstub failure
- the next smallest visible proof point is selected and documented
- the selected next step stays independent of UART and framebuffer availability

## Validation Plan

- use the current board evidence:
  - ACT LED steady on
  - no screen output
  - no keyboard-visible reaction
- compare the current earliest-entry proof locations in:
  - the custom Pi 4 armstub
  - early `plo` entry
- choose the smallest later milestone that can be signaled on the same ACT LED

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-04-08-pi4-sdimg-export-fix.md`

## Notes

- The latest board result proves that the custom Pi 4 armstub now executes.
- The next useful split is no longer "firmware reached our armstub or not?".
- The next useful split is:
  - armstub runs but `plo` entry never begins
  - `plo` entry begins but later early init fails
