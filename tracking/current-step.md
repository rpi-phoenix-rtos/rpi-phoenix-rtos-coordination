# Current Step

## Metadata

- Step ID: `STEP-0508`
- Title: `Retry Pi 4 on the pre-MMU page-table invalidation image`
- Status: `ready`
- Date: `2026-04-17`
- Milestone / phase: `Phase 1`

## Objective

- retry the Pi 4 on the image that adds Linux-style pre-MMU cache maintenance
  for the early TTBR0 / TTBR1 tables
- verify whether the real hardware path finally moves beyond the longstanding
  `A2 / KLM / X1 / X2 / X3` seam

## Scope

In scope:

- one real-device retry on the refreshed image
- UART capture with the canonical helper
- no LED diagnostics
- no new source probes before the retry result

Out of scope:

- more kernel probe churn
- broader userspace tracing
- DTB refactors before this hardware retry

## Acceptance Criteria

- the refreshed image is tried on real hardware
- the retry captures at least one raw UART log
- the retry shows whether execution moves beyond:
  - `A2`
  - `KLM`
  - `X1`
  - `X2`
  - `X3`
- the retry proves whether the restored post-MMU seam is reached:
  - `N`
  - `O`
  - `P`
  - `Q`
  - `R`
  - `S`

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
  - `X3`
  - `N`
  - `O`
  - `P`
  - `Q`
  - `R`
  - `S`

## Rollback / Baseline

- current exported image to test:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
  (SHA-256: `14553eb250414b6b93e72cca44f280aac88d5162fdb57aa7f6ae9a659c3e68b5`)

## Notes

- the stale-image theory has been disproved for the current artifact chain
- the exact rollback image previously stopped on real hardware at:
  - `A2`
  - `KLM`
  - `X1`
  - `X2`
  - `X3`
- a bounded Pi 4 QEMU gdbstub session proved that same image still reaches:
  - `_core_0_virtual`
  - `_set_up_vbar_and_stacks`
  - `main()`
  under emulation
- the current image keeps the earlier `TTBR1`-from-start structure and adds the
  strongest remaining Linux-style fix:
  - invalidate the contiguous early page-table region with `dc ivac`
    before MMU-on
