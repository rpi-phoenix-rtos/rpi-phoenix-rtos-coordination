# Current Step

## Metadata

- Step ID: `STEP-0441`
- Title: Await the next Pi 4 board retry on the structured LED telemetry image
- Status: `pending`
- Date: `2026-04-09`
- Milestone / phase: `Phase 1`

## Objective

- collect the next real Pi 4 hardware answer from the first structured
  ACT-LED telemetry image so the highest completed boot checkpoint can be
  identified from one high-framerate LED video

## Scope

In scope:

- rewriting the SD card from the refreshed verified telemetry image
- booting the real Pi 4 board
- recording a high-quality ACT-LED video from power-on through steady state
- recording:
  - final ACT LED state and pulse groups
  - blank-or-visible HDMI result
  - any keyboard-visible reaction

Out of scope:

- new code changes before the next board result
- unrelated USB, framebuffer, or shell-runtime changes

## Expected Repositories

- coordination repo

## Expected Files Or Subsystems

- `tracking/current-step.md`
- `tracking/step-history.md`
- `docs/status.md`
- `docs/manual-operator-instructions.md`
- `docs/pi4-first-hardware-trial.md`
- `docs/testing-automation.md`
- `manifests/2026-04-09-pi4-led-telemetry-protocol.md`

## Acceptance Criteria

- the next board retry reports the observed ACT LED pulse groups
- the result is paired with:
  - screen state
  - any keyboard-visible reaction
  - whether the board appears to hang or reset
- the next implementation step can choose the exact earliest failing checkpoint
  instead of moving another one-off probe

## Validation Plan

- rewrite the SD card from the refreshed exported image
- boot the real Pi 4 and record a high-framerate close-up of the LEDs
- classify the highest completed telemetry group before making any new code
  change

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-04-09-pi4-led-telemetry-protocol.md`

## Notes

- The current image replaces the old one-off GPIO42 proofs with numbered stage
  groups:
  - `1`: armstub primary-core entry
  - `2`: armstub after early timer / GIC preparation
  - `3`: armstub just before the fixed-address jump to `plo`
  - `4`: earliest generic AArch64 `plo` `_start`
  - `5`: `plo` EL3 path selected
  - `6`: `plo` EL2 path selected
  - `7`: `plo` EL1 path selected
  - `8`: `plo` `start_common`
  - `9`: `plo` core-0 branch to `_startc`
- Each checkpoint is emitted as one pulse group separated by longer off gaps.
- The current refreshed exported image is:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- Current validated SHA-256:
  `6d6b4d7dd84f237f3e8dab1764f8be34b29b4e4d46d6f92ad30aee1869a2acdc`
