# Current Step

## Metadata

- Step ID: `STEP-0507`
- Title: `Retry Pi 4 on the TTBR1-from-start kernel image`
- Status: `ready`
- Date: `2026-04-17`
- Milestone / phase: `Phase 1`

## Objective

- retry the Pi 4 on the restructured kernel MMU-transition image
- verify whether removing the runtime TTBR1-activation seam restores later
  post-MMU progress on real hardware

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
  (SHA-256: `f65877d5cffc58222198cc71f2841a09b3d183b4fb66b92e9efaa2e52fe171aa`)

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
- the current image therefore removes the remaining Phoenix-specific seam:
  - TTBR1 is now built and enabled before MMU-on
  - the late runtime `TCR_EL1` toggle is gone
