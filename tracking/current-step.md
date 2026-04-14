# Current Step

## Metadata

- Step ID: `STEP-0483`
- Title: Await the next Pi 4 board retry with clean stabilized image
- Status: `in_progress`
- Date: `2026-04-12`
- Milestone / phase: `Phase 1`

## Objective

- verify that the stable userspace boot path is restored (psh prompt on HDMI)
- confirm that the legacy LED blinks are gone, resulting in a faster boot process
- capture a successful UART log using the 103448 baud rate (to verify if our kernel UART init revert helps or hurts)
- verify that the PCIe `va2pa` fix persists and eliminates the `SError` exception

## Scope

In scope:
- analysis of the next real-device trial results
- verification of clean boot sequence (no old LED bursts)
- verification of `psh` accessibility

Out of scope:
- broad driver changes before seeing the next log

## Acceptance Criteria

- `psh` prompt appears on HDMI console
- no legacy LED diagnostic signals are seen
- `SError` exception no longer appears in the `pcie` or `usb` threads

## Validation Plan

- analyze the next log and video/screenshot
- use the results to choose between final UART baud-rate refinement or first shell-based hardware tests

## Rollback / Baseline

- latest clean image:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
  (SHA-256: `3de5a1f3cbda75fd848bc5627063a6e620775e531f8b2db2c1fd6e96146898f3`)

## Notes

- all manual HDMI mirroring was removed to fix the recent regression
- legacy LED probes were purged to satisfy the speed-up request
- the PCIe `va2pa` fix is the only significant logic change kept from the previous failed image
