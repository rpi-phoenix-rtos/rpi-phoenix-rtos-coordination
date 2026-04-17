# Current Step

## Metadata

- Step ID: `STEP-0499`
- Title: `Retry Pi 4 on the post-MMU UART split image`
- Status: `ready`
- Date: `2026-04-17`
- Milestone / phase: `Phase 1`

## Objective

- retry the Pi 4 with the refreshed image that adds safe post-MMU kernel UART
  breadcrumbs
- classify the remaining real-hardware boundary between:
  - `ttbr1`-backed post-MMU UART enable
  - `_core_0_virtual`
  - syspage copy
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
- the retry shows the highest completed kernel breadcrumb among:
  - `KLM`
  - `KLMN`
  - `KLMNO`
  - `KLMNOP`
  - `KLMNOPQ`
  - `KLMNOPQR`
  - `KLMNOPQRS`
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
  - `N`
  - `O`
  - `P`
  - `Q`
  - `R`
  - `S`

## Rollback / Baseline

- current exported image to test:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
  (SHA-256: `6638e81ec8052beb23bb83a02340b1a1cc3a1e4914ce2c0779b949c04d275c9a`)

## Notes

- the latest real-board UART log
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b-uart/rpi4b-uart-20260417-211048.log`
  already proves:
  - firmware handoff is working
  - custom armstub UART recovery is working
  - reloc trampoline UART recovery is working
  - `plo` reaches and exits via:
    - `hal: jump entry`
    - `hal: jump irq off`
    - `hal: jump exit el1`
  - kernel `_start` reaches:
    - `K`
    - `L`
    - `M`
- the active blocker is now after `M` and before `main()`
- raw physical PL011 writes are no longer trustworthy after the MMU/TTBR1
  transition, so the current image now uses a fixed temporary PL011 virtual
  mapping for post-MMU breadcrumbs
