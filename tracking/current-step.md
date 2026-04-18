# Current Step

## Metadata

- Step ID: `STEP-0518`
- Title: `Verify Pi 4 recovery from UART identity-map and SMP regressions`
- Status: `in_progress`
- Date: `2026-04-19`
- Milestone / phase: `Phase 1`

## Objective

- verify that the fix for `EARLY_UART_DEVICE_BLOCK` (L1 block descriptor)
  resolves the hang at `X3`
- verify that enabling SMP for A72 and switching to Non-shareable early boot
  memory improves stability
- regain post-MMU UART visibility and ideally proceed to the kernel banner
- capture diagnostic data if any further early exceptions occur

## Scope

In scope:

- validation of the `phoenix-rtos-kernel` fixes in `_init.S`
- one rebuilt and re-exported Pi 4 image
- one real-device UART retry on that image

Out of scope:

- broad peripheral driver work
- unrelated `plo` refactoring

## Acceptance Criteria

- the boot process proceeds beyond marker `X3`
- markers `N` (78) and `O` (79) are visible on real hardware
- the kernel reaches `main()` or emits a valid exception report (EX=...)
- the `... X3NO` baseline is not only restored but advanced

## Validation Plan

- rebuild and flash:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- capture UART with:
  - `/Users/witoldbolt/phoenix-rpi/scripts/capture-rpi4b-uart.sh`
- inspect for:
  - `X3`
  - `N`
  - `O`
  - `P`
  - `Q`
  - `R`
  - `S`
  - kernel banner

## Rollback / Baseline

- recent neutral hardware retries:
  - `phoenix-rtos-kernel 6cd294fd`
    image `5ac0d1290867556a78fe19bad048b1cfe98e8c5328053c2d588ed0d8691006fe`
    log `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b-uart/rpi4b-uart-20260418-115137.log`
  - `phoenix-rtos-kernel 136b4cae`
    image `f44385750b37adc49bb279156e812e561c61ec8d31b983fae457215cd0fab469`
    log `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b-uart/rpi4b-uart-20260418-220352.log`
- restored rollback baseline proved on hardware:
  - `phoenix-rtos-kernel 91f5f9d5`
  - `phoenix-rtos-project e8f794f`
  - image `be8c2773306870a5b66b75f64677d68d0a344f01ee348d2e1598aea969ca4fb1`
  - log `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b-uart/rpi4b-uart-20260418-222500.log`
  - restored seam: `A2 KLM X1 X2 X3 NO`
- disproved re-split experiment:
  - `phoenix-rtos-kernel 5f3bf75e`
  - image `ff1b0ca7b4bb89f4f8812537750487566160fc4e583368748976f80b4c200cb4`
  - log `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b-uart/rpi4b-uart-20260418-234332.log`
  - regressed seam: `A2 KLM X1 X2 X3`
- refreshed rollback image now ready:
  - `phoenix-rtos-kernel a4883d37` restoring the `91f5f9d5` lineage in
    `_init.S`
  - `phoenix-rtos-project e8f794f`
  - image `576bacf524d115f8f99361d0434eac32a92d0f1354f8169fb5c7fa24502f39d8`

## Notes

- the stale-image theory has already been disproved for this artifact chain
- `STEP-0515` is still the strongest real-board checkpoint restoration so far:
  the rollback successfully restored `... X3NO`
- `STEP-0516` is now closed as a negative hardware result: the finer
  `NO -> P` re-split regressed back to `X3`
- there is no hardware-backed evidence in the project history for a later UART
  seam than `... X3NO`; apparent later milestones in manifests are QEMU-only
  or HDMI-only observations
