# Current Step

## Metadata

- Step ID: `STEP-0459`
- Title: Await the next Pi 4 board retry on the dense armstub signature-map image
- Status: `in_progress`
- Date: `2026-04-10`
- Milestone / phase: `Phase 1`

## Objective

- collect the next board retry on the dense armstub image and use the much
  finer armstub-side telemetry to isolate the failing instruction band tightly
  enough that the next code step can be an actual boot fix rather than another
  broad instrumentation round

## Scope

In scope:

- decode the next board video against the dense armstub map
- classify the exact highest completed late armstub stage before changing more
  boot logic

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

- the refreshed dense-probe image is rebuilt, exported, and FAT-verified
- the next board video can distinguish:
  - failure before the late armstub seam starts
  - failure on fixed-target address load
  - failure on first or second signature-word read
  - first compare mismatch versus second compare mismatch
  - failure after both compares pass but before successful `plo` veneer entry
  - EL2 exception trap during that band

## Validation Plan

- board retry plus LED decode
- use the resulting exact boundary to choose the first non-diagnostic boot fix

## Rollback / Baseline

- Known-good manifest or commit set:
  `/Users/witoldbolt/phoenix-rpi/manifests/2026-04-10-pi4-led-analysis-toolchain.md`

## Notes

- the dense armstub signature-map image is now built:
  - `23`: late seam entered
  - `24`: fixed target address loaded
  - `25`: first signature word read
  - `26`: second signature word read
  - `27`: first expected signature constant loaded
  - `28`: first compare passed
  - `29`: second expected signature constant loaded
  - `30`: second compare passed
  - `4`: signature verified before branch
  - `31`: mismatch halt
  - `0`: EL2 exception trap
- current exported SD-image SHA-256:
  - `6b349fe6c2afe11ea0fdeb5d9fc874eb5ae1b990ee83d42c48f10662445875e8`
- initial SD-read LED chatter remains firmware preamble noise and should be
  ignored unless it participates in a later valid contiguous Phoenix decode
