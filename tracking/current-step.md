# Current Step

## Metadata

- Step ID: `STEP-0415`
- Title: Await the next Pi 4 board retry on the armstub rebuild
- Status: `in_progress`
- Date: `2026-04-08`
- Milestone / phase: `Phase 1`

## Objective

- hold the project at the next justified real-hardware boundary and collect the
  first board evidence from the refreshed Pi 4 SD image that now includes the
  custom `phoenix-armstub8-rpi4.bin` handoff stub

## Scope

In scope:

- the next real Pi 4 SD-card boot on the refreshed exported image
- recording the exact HDMI, LED, keyboard, and reboot behavior
- mapping that evidence to the smallest next code or image step

Out of scope:

- speculative source changes before the next board retry result exists
- wider refactors unrelated to the observed next hardware symptom

## Expected Repositories

- coordination repo

## Expected Files Or Subsystems

- `docs/pi4-first-hardware-trial.md`
- `docs/manual-operator-instructions.md`
- `docs/status.md`
- `tracking/step-history.md`
- `manifests/`

## Acceptance Criteria

- the refreshed Pi 4 SD image has been handed off with its exact checksum
- the next board retry result has been captured
- the next active implementation step is chosen from real hardware evidence,
  not guesswork

## Validation Plan

- use the exported image:
  - `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- verify with:
  - `/Users/witoldbolt/phoenix-rpi/scripts/verify-rpi4b-sdimg.sh`
- flash and boot according to:
  - `/Users/witoldbolt/phoenix-rpi/docs/pi4-first-hardware-trial.md`

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-04-08-pi4-armstub-rebuild.md`

## Notes

- Current exported image SHA-256:
  `9fc6dd1b5c6a5da81aa62c980f5abbc68f165183a0efa084881cb81202d38e24`
- The previous low-placement experiment is closed as false.
- The next stronger signal must come from the real board, not from more QEMU-
  only source changes.
