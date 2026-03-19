# Manifest: First Runtime-Oriented AArch64 Step Scope

- Date: `2026-03-20`
- Step: `STEP-0014`
- Result: `completed`

## Scope

- inspect the AArch64 kernel runtime dependencies after the DTB preparation series
- compare the smallest realistic follow-up options:
  - a generic ARM architectural timer implementation path
  - a broader generic AArch64 target or platform split
- select one narrow runtime-oriented implementation step

## Result

- selected next implementation step:
  remove the hard compile-time timer IRQ dependency from the common AArch64 GICv2 code by moving timer IRQ knowledge behind the timer HAL API

## Why This Step Was Selected

- `hal/aarch64/interrupts_gicv2.c` is common AArch64 code, but it still depends on the platform macro `TIMER_IRQ_ID` only to suppress timer IRQ trace noise.
- That compile-time dependency blocks clean reuse of the common GICv2 path for non-ZynqMP AArch64 targets.
- Replacing it with a timer HAL query is smaller than introducing a generic ARM timer backend and much smaller than creating a broader generic AArch64 target split.
- This step stays entirely inside `phoenix-rtos-kernel` and has a clear validation lane:
  `TARGET=aarch64a53-zynqmp-qemu ./phoenix-rtos-build/build.sh clean host core project`

## Compared Alternatives

### Generic ARM Architectural Timer Runtime

- useful, but it would require introducing new runtime code, choosing the first usable architectural timer interrupt source, and deciding how that code is selected by targets
- that is a valid near-term follow-up, but it is still wider than necessary for the first runtime-oriented step

### Broader Generic AArch64 Target Or Platform Split

- this would immediately widen into `phoenix-rtos-build`, `phoenix-rtos-project`, and likely `plo`
- it is too broad for the next controlled step

## Selected Next Step

- add a timer IRQ query to the timer HAL interface
- implement that query in the current ZynqMP AArch64 timer backend
- update the common AArch64 GICv2 code to use the timer HAL query instead of the `TIMER_IRQ_ID` macro
