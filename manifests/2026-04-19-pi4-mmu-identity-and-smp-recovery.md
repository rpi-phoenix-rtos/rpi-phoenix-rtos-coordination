# Pi 4 MMU Identity and SMP Recovery

Date: 2026-04-19

## Summary

Analysis of the real-board UART log `artifacts/rpi4b-uart/rpi4b-uart-20260418-235652.log`
and subsequent code review identified a critical regression and a major bug:

1.  **UART Identity Mapping Corruption:** The `EARLY_UART_DEVICE_DESCR` was
    incorrectly defined as a TABLE descriptor (bit 1=1) but was used in the L1
    identity mapping table. This caused the CPU to walk into garbage data when
    accessing the UART after MMU-on.
2.  **Missing SMP Enablement for A72:** The SMP bit in `CPUECTLR_EL1` was only
    being set for A53 targets, leaving the A72 (Pi 4) with undefined coherency
    behavior when using `Inner Shareable` memory.
3.  **Garbage in Identity Mapping Tables:** The `PMAP_COMMON_SCRATCH_TT` page
    was not zeroed, potentially leading to speculative walk failures.
4.  **Early Exception Visibility:** The early exception handler and vector
    table had been removed, leaving the system "blind" during early MMU faults.

## Changes

### `phoenix-rtos-kernel`

-   **`hal/aarch64/_init.S`**:
    -   Restored `PMAP_COMMON_SCRATCH_TT` zeroing before use.
    -   Fixed `EARLY_UART_DEVICE_BLOCK` to use a proper L1 BLOCK descriptor.
    -   Enabled SMP bit in `CPUECTLR_EL1` for `__TARGET_AARCH64A72`.
    -   Changed `TCR_EL1` to use `Non-shareable` memory for early boot to
        improve stability before SMP is fully active.
    -   Restored `_early_vector_table` and exception reporting code.

## Impact

These changes resolve the immediate hang at `X3` by fixing the invalid page
table entry for the UART and ensuring the root table is clean. The restoration
of the exception handler will provide diagnostic data (ESR, ELR, FAR) if further
faults occur.

## Validation

-   Requires real-board UART retry.
-   Expected to reach at least marker `N` (78) and `O` (79) after MMU-on.

## Source Commits

- `phoenix-rtos-kernel`: `06b9da4b`

