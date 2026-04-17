# Current Step

## Metadata

- Step ID: `STEP-0506`
- Title: `Retry Pi 4 on the rolled-back post-MMU UART baseline with Linux-style MMU I-cache sync`
- Status: `ready`
- Date: `2026-04-17`
- Milestone / phase: `Phase 1`

## Objective

- stop iterating on the regressed `X3`-only kernel line
- retry the Pi 4 on the last objectively better post-MMU UART baseline
- validate whether the Linux-style post-`SCTLR_EL1` I-cache invalidation fix
  restores the proven `N O P Q R S` seam or moves the boundary forward

## Scope

In scope:

- one real-device retry on the refreshed rollback image
- UART capture with the canonical helper
- no LED diagnostics
- no new source probes before the retry result

Out of scope:

- broader userspace tracing
- new DTB refactors
- new MMU design changes before this rollback-based retry is tested

## Acceptance Criteria

- the refreshed rollback image is tried on real hardware
- the retry captures at least one raw UART log
- the retry shows whether the board again reaches the recovered late seam:
  - `N`
  - `O`
  - `P`
  - `Q`
  - `R`
  - `S`
- the next engineering change can target one precise sub-band instead of
  continuing the current probe churn

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
  (SHA-256: `5eb05cc13844cf6628b1334753e112c59e90303c45feedc9a294bc1760051700`)

## Notes

- the latest failing real-board UART log
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b-uart/rpi4b-uart-20260417-223918.log`
  still ends at:
  - `A2`
  - `KLM`
  - `X1`
  - `X2`
  - `X3`
- however, earlier logs `213826` and `215745` objectively reached `... X3NO`
  on real hardware, so the active strategy is now:
  - restore that better baseline
  - keep only one primary-source-backed MMU transition fix on top
- the current image therefore:
  - restores the temporary post-MMU PL011 virtual mapping
  - restores the `N O P Q R S` seam that had already worked on hardware
  - removes the later regressing syspage and TTBR1 experiments
  - adds Linux-style local I-cache invalidation immediately after
    `msr sctlr_el1, x0` when enabling the MMU
