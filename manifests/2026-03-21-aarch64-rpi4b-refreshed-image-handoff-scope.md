# 2026-03-21: scope the smallest refreshed Pi 4 image handoff step

## Scope

- Step: `STEP-0288`
- Goal: select the smallest post-build handoff step after the Pi 4 HDMI
  firmware refinement

## Current State

- the Pi 4 firmware staging config was refined and validated in the VM build
  output
- the host-visible artifact path used for manual flashing already exists:
  - `artifacts/rpi4b/rpi4b-sd.img`
- the project already has a purpose-built export helper:
  - `scripts/export-rpi4b-sdimg.sh`

## Decision

The smallest next step should be:

- rerun the existing full-disk export helper
- verify the refreshed host-visible image exists
- record its new SHA-256 in the coordination repo

## Why This Is The Smallest Useful Step

- it reuses the established artifact path and operator workflow
- it ensures the first real Pi 4 board trial uses the refined HDMI config
- it does not widen into flashing automation or real hardware execution

## Out Of Scope

- changing the export path
- writing the image to SD automatically
- running the board

## Next Step

- implement `STEP-0289`: refresh the host-visible Pi 4 SD image with
  `scripts/export-rpi4b-sdimg.sh` and record the updated artifact checksum
