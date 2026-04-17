# Current Step

## Metadata

- Step ID: `STEP-0510`
- Title: `Capture the first post-3C Pi 4 kernel MMU exception`
- Status: `in_progress`
- Date: `2026-04-17`
- Milestone / phase: `Phase 1`

## Objective

- replace the silent post-`3C` stall with a direct exception-capture path
- detect whether the first hardware-only failure after `SCTLR_EL1` is a
  synchronous abort, SError, or another early exception
- emit the exception slot plus `ESR_EL1`, `ELR_EL1`, and `FAR_EL1` through the
  identity-mapped physical PL011 path

## Scope

In scope:

- one kernel `_init.S` change set to install an early exception VBAR before
  MMU-on
- one TTBR0 identity-map extension for the PL011 1 GB block so that the early
  exception path can still print after MMU-on
- one rebuilt and re-exported Pi 4 image
- one real-device UART retry on that image

Out of scope:

- broader userspace tracing
- unrelated DTB refactors
- more LED work

## Acceptance Criteria

- the refreshed image is tried on real hardware
- the retry captures at least one raw UART log
- the retry either:
  - emits an early exception report after `3C`, or
  - proves that no early exception was taken at the old seam
- the retry gives a concrete next root-cause direction instead of another
  silent stop at the same marker

## Validation Plan

- flash:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- capture UART with:
  - `scripts/capture-rpi4b-uart.sh --profile firmware ...`
- inspect the resulting log for:
  - `AS0`
  - `TR0..TR3`
  - `hal: jump exit el1`
  - `A2`
  - `KLM`
  - `X1`
  - `X2`
  - `3C`
  - `EX=`
  - `ESR=`
  - `ELR=`
  - `FAR=`
  - any later normal kernel console output

## Rollback / Baseline

- current exported image to test:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
  (SHA-256: `4e873f294f07e6d636390816aac318b51f3ceb55ed85ab4ea9ac594e0fc06204`)
- previous image before this strategy change:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
  (SHA-256: `bc08128b86c3d7b22cbd1160b81281d0ef5849c34c88f962b3cadfad29aa559d`)

## Notes

- the stale-image theory has been disproved for the current artifact chain
- the latest real-board UART log
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b-uart/rpi4b-uart-20260417-234142.log`
  still stopped at:
  - `A2`
  - `KLM`
  - `X1`
  - `X2`
  - `3C`
- earlier real-board logs on the same day genuinely reached `...X3NO`, so the
  project is not stuck on an imaginary or stale-image boundary; it lost
  observability after the post-MMU UART path was removed
- the next move is therefore to catch the first real exception at the seam,
  not to add more late-stage progress markers
