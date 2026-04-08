# Current Step

## Metadata

- Step ID: `STEP-0432`
- Title: Retry the Pi 4 board boot on the post-armstub `plo` LED-split image
- Status: `in_progress`
- Date: `2026-04-08`
- Milestone / phase: `Phase 1`

## Objective

- retry the Pi 4 board with the image that now distinguishes custom armstub
  reachability from early `plo` `_startc()` reachability

## Scope

In scope:

- flashing the refreshed Pi 4 SD image
- observing the next board result with the new two-stage ACT LED semantics
- classifying the result before widening the boot diagnosis again

Out of scope:

- wide boot redesigns
- unrelated USB, PCIe, or runtime shell work
- new runtime experiments before the new board result arrives

## Expected Repositories

- none unless the new board result requires the next implementation step

## Expected Files Or Subsystems

- `tracking/current-step.md`
- `docs/pi4-first-hardware-trial.md`
- `docs/manual-operator-instructions.md`

## Acceptance Criteria

- the refreshed image is flashed to the whole SD-card device
- the board result records whether the ACT LED:
  - stays on
  - or turns on and later ends off
- the next implementation step is chosen from that split

## Validation Plan

- run `scripts/verify-rpi4b-sdimg.sh`
- flash the whole-card image
- boot the board and capture:
  - final ACT LED state
  - screen state
  - any keyboard-visible effect

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-04-08-pi4-plo-entry-led-proof.md`

## Notes

- The current exported image SHA-256 is:
  `acea299fb225edb0293b4d022b9b19d984fe51627a168bd69c403442590b757d`.
- The expected split is now:
  - ACT LED stays on: failure before `_startc()`
  - ACT LED ends off: `_startc()` reached, failure later in early `plo`
