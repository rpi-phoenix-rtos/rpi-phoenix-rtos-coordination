# Current Step

## Metadata

- Step ID: `STEP-0460`
- Title: Split the Pi 4 dense armstub seam around the first fixed-target read
- Status: `in_progress`
- Date: `2026-04-10`
- Milestone / phase: `Phase 1`

## Objective

- use the new `IMG_0012.mov` dense-map result to attack the first fixed-target
  read band directly, because the current board evidence now reaches stage
  `24` and stops before the first read-complete stage `25`

## Scope

In scope:

- split or harden the exact band between:
  - `24`: fixed target address loaded
  - `25`: first signature word read
- decide whether the next change should be:
  - a finer pre/post-read probe
  - a no-call inline marker around the first read
  - or an actual replacement of the fixed-target read strategy

Out of scope:

- unrelated EL-path, framebuffer, DTB, or USB work
- redesigning the whole Pi 4 boot model before the dense armstub map is tested

## Expected Repositories

- `phoenix-rtos-project`
- `plo`
- coordination repo

## Expected Files Or Subsystems

- `/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/phoenix-armstub8-rpi4.S`
- `/Users/witoldbolt/phoenix-rpi/scripts/rpi4_actled_probe_layout.py`
- `/Users/witoldbolt/phoenix-rpi/scripts/verify-rpi4b-sdimg.sh`
- `/Users/witoldbolt/phoenix-rpi/docs/status.md`
- `/Users/witoldbolt/phoenix-rpi/tracking/current-step.md`

## Acceptance Criteria

- the next code step narrows the live seam around the first signature-word read
- the next board video can distinguish whether the failure is:
  - on the first fixed-target read itself
  - immediately after the first read but before the stage-`25` emission
  - or later, with the current stage-`25` absence explained as decode loss

## Validation Plan

- use the already-collected `IMG_0012.mov` decode as the current boundary
- rebuild one narrower or more robust first-read experiment
- board retry plus LED decode

## Rollback / Baseline

- Known-good manifest or commit set:
  `/Users/witoldbolt/phoenix-rpi/manifests/2026-04-10-pi4-led-analysis-toolchain.md`

## Notes

- `IMG_0012.mov` on the dense-map image decodes as the current best contiguous
  Phoenix run:
  - `2`: armstub after timer/GIC
  - `3`: armstub before fixed jump
  - `23`: late seam entry
  - `24`: fixed target address loaded
- the next expected stage is:
  - `25`: first signature word read
- no later valid stage `25`, `31`, or `0` was seen in the main run
- one lone later unmatched stage `27` burst exists, but it is not currently
  trusted as the main run because the contiguous `25 -> 26` prefix is absent
- current exported SD-image SHA-256 remains:
  - `6b349fe6c2afe11ea0fdeb5d9fc874eb5ae1b990ee83d42c48f10662445875e8`
