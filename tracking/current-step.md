# Current Step

## Metadata

- Step ID: `STEP-0504`
- Title: `Retry Pi 4 after reverting the syspage-buffer regression`
- Status: `ready`
- Date: `2026-04-17`
- Milestone / phase: `Phase 1`

## Objective

- retry the Pi 4 with the refreshed image that restores the original post-MMU
  syspage copy seam and also restores the original one-page syspage backing,
  while keeping the finer UART breadcrumbs inside `O -> P`
- verify whether the active boundary moves past:
  - the old `... NO` seam
  - post-MMU syspage variable initialization
  - size load
  - first copy iteration
  - `_set_up_vbar_and_stacks`
  - earliest `main()`

## Scope

In scope:

- one real-device retry on the refreshed image
- UART capture with the canonical helper
- no LED dependence
- no new broad tracing before the retry result

Out of scope:

- restoring LED diagnostics
- broader userspace tracing
- unrelated DTB/runtime refactors before the next retry

## Acceptance Criteria

- the refreshed image is tried on real hardware
- the retry captures at least one raw UART log
- the retry shows whether the raw tail returns from `... X3` to at least
  `... NO`, or advances further through the narrower `OUVWZYP` seam
- the next engineering change can target one precise sub-band of the kernel
  MMU-to-`main()` path

## Validation Plan

- flash:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- capture UART with:
  - `scripts/capture-rpi4b-uart.sh --profile firmware ...`
- inspect the resulting log for:
  - `AS0`
  - `TR0..TR3`
  - `hal: jump exit el1`
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
  (SHA-256: `51d4f610d6bbc7778e5de165add6ff0be908879396da859f75323aef14fb6d8c`)

## Notes

- the latest real-board UART log
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b-uart/rpi4b-uart-20260417-221842.log`
  proved the previous enlarged syspage buffer was also part of the regression:
  - the raw tail regressed from:
    - `A2`
    - `KLM`
    - `X1`
    - `X2`
    - `X3`
    - `NO`
  - back to only:
    - `A2`
    - `KLM`
    - `X1`
    - `X2`
    - `X3`
- that means:
  - moving the syspage copy before the MMU jump made the live hardware
    boundary earlier
  - keeping `_hal_syspageCopied = 16 * SIZE_PAGE` was still enough to hold the
    boundary at `... X3`, so that BSS-layout change is also not safe yet
- the refreshed image therefore:
  - keeps the restored post-MMU copy in `_core_0_virtual`
  - reverts `_hal_syspageCopied` back to `SIZE_PAGE`
  - adds finer UART breadcrumbs:
    - `U` after `relOffs` store
    - `V` after `hal_syspage` store
    - `W` after `syspage->size` load
    - `Z` before the first copy iteration
    - `Y` after the first 8-byte copy iteration
    - `P` after full copy completion
