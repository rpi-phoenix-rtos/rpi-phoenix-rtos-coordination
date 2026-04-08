# Current Step

## Metadata

- Step ID: `STEP-0435`
- Title: Await the next Pi 4 board retry on the pre-kernel-branch armstub LED image
- Status: `pending`
- Date: `2026-04-08`
- Milestone / phase: `Phase 1`

## Objective

- collect the next real-hardware answer after proving that:
  - the custom armstub executes on the board
  - late `plo` `_init.S` is still not reached

## Scope

In scope:

- flashing the refreshed Pi 4 SD image
- booting the real Pi 4 board
- recording the final ACT LED state, blank-or-visible HDMI result, and any
  keyboard-visible reaction

Out of scope:

- new code changes before the next board result
- unrelated runtime, USB, or framebuffer changes

## Expected Repositories

- coordination repo

## Expected Files Or Subsystems

- `tracking/current-step.md`
- `tracking/step-history.md`
- `docs/status.md`
- `docs/pi4-first-hardware-trial.md`
- `docs/manual-operator-instructions.md`
- `docs/source-artifacts.md`
- `manifests/2026-04-08-pi4-pre-kernel-branch-led-proof.md`

## Acceptance Criteria

- the next board retry reports one of:
  - ACT LED stays on
  - ACT LED ends off
- the result is paired with:
  - screen state
  - any keyboard-visible reaction
- the next implementation step can then choose between:
  - earlier armstub diagnosis
  - branch-to-kernel diagnosis
  - earliest `plo`/kernel-entry diagnosis

## Validation Plan

- rewrite the SD card from the refreshed exported image
- boot the real Pi 4 and observe ACT LED, screen, and keyboard behavior

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-04-08-pi4-pre-kernel-branch-led-proof.md`

## Notes

- The temporary late-`_init.S` proof image was already tested on the board and
  disproved:
  the final ACT LED still stayed on, so the late `plo` split was removed
  instead of being committed.
- In the current image:
  - the custom armstub still drives GPIO42 high first
  - the primary-core armstub path now drives GPIO42 low just before branching
    to `kernel8.img`
  - final ACT LED on means the failure is still before that final armstub
    handoff point
  - final ACT LED off means the branch-site handoff was reached and the next
    failure is later
