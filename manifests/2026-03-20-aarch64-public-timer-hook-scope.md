# Manifest: First AArch64 Public Timer Hook Scope

- Date: `2026-03-20`
- Step: `STEP-0041`
- Result: `completed`

## Scope

- inspect the AArch64 timer build hook after `STEP-0040`
- choose the smallest build-structure step needed before a common public timer-HAL wrapper can be added
- select the exact touched files for that step

## Result

- selected first public-timer wrapper preparation slice:
  split the existing AArch64 timer object hook so the public timer implementation object can be selected explicitly, while preserving the current ZynqMP timer implementation
- selected responsibilities:
  - keep build behavior unchanged for the existing `aarch64a53-zynqmp-qemu` lane
  - prepare the build glue so a future common `gtimer`-based public timer implementation can be selected without colliding with the current platform timer object
  - restrict the step to AArch64 Makefile glue only
- selected exact file set:
  - `phoenix-rtos-kernel/hal/aarch64/Makefile`
  - `phoenix-rtos-kernel/hal/aarch64/zynqmp/Makefile`
- selected validation:
  rebuild the existing `aarch64a53-zynqmp-qemu` lane in `phoenix-dev`

## Why This Was Selected

- The helper layer is now ready, but a future common public timer implementation still needs a clear build-selection path.
- A small build-glue split is lower risk than jumping straight to a new public timer file with duplicate `hal_timer*` symbols.
- This keeps the next step behavior-preserving while unblocking the first real common timer-HAL wrapper.

## Selected Next Step

- implement an explicit AArch64 public timer-implementation object hook while keeping the current ZynqMP timer implementation selected
