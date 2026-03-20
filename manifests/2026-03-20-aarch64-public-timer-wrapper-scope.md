# Manifest: First Validated Public AArch64 Timer Wrapper Scope

- Date: `2026-03-20`
- Step: `STEP-0043`
- Result: `completed`

## Scope

- inspect the new public timer-implementation hook after `STEP-0042`
- choose the smallest validated path for the first common public AArch64 `hal_timer*` wrapper step
- select the exact touched files for that preparatory step

## Result

- selected first validated public-wrapper preparation slice:
  add an explicit AArch64 timer-implementation override hook so a future common public timer implementation can be compiled through a kernel-only validation lane without replacing the current platform timer by default
- selected responsibilities:
  - keep the existing `aarch64a53-zynqmp-qemu` behavior unchanged by default
  - allow a future validation build to select a common public timer implementation explicitly
  - restrict the step to AArch64 Makefile glue only
- selected exact file set:
  - `phoenix-rtos-kernel/hal/aarch64/Makefile`
  - `phoenix-rtos-kernel/hal/aarch64/zynqmp/Makefile`
- selected validation:
  rebuild the existing `aarch64a53-zynqmp-qemu` lane in `phoenix-dev`

## Why This Was Selected

- The helper layer and public-timer hook are ready, but the first public common timer file still needs a safe way to compile without silently replacing the current platform timer.
- A dedicated override hook is narrower and more upstreamable than inventing a whole new target just to compile the first wrapper file.
- This keeps the next step behavior-preserving while creating a clear validation path for the next common timer wrapper step.

## Selected Next Step

- implement an explicit AArch64 timer-implementation override hook while keeping ZynqMP selected by default
