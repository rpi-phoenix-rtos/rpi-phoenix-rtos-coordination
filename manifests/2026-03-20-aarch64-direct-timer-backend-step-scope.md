# Manifest: First Directly Selectable AArch64 Timer Backend Step Scope

- Date: `2026-03-20`
- Step: `STEP-0021`
- Result: `completed`

## Scope

- inspect the current AArch64 timer preparation work after `STEP-0020`
- choose the first directly selectable common timer backend shape
- select the smallest exact touched-file set for the next implementation step

## Result

- confirmed eventual backend shape:
  a directly selectable common AArch64 generic timer backend still remains the target design
- identified the first blocking constraint:
  `hal_timerSetWakeup()` is currently used from scheduler paths that are not restricted to CPU 0, while an ARM architectural timer backend would initially be CPU-local and therefore cannot be introduced safely as a drop-in replacement yet
- identified a second smaller correctness constraint:
  the common AArch64 GICv2 handler-registration path still applies CPU targeting unconditionally, even though the future architectural timer IRQ will be a PPI rather than an SPI
- selected next implementation step:
  add explicit AArch64 timer-backend selection scaffolding in the kernel Makefiles while keeping the current ZynqMP timer backend and runtime behavior unchanged

## Why This Was Selected

- The DTB source-selection API and architectural timer sysreg helpers now exist, but the runtime timer contract is still shaped around a shared programmable source.
- The scheduler wakeup path in `proc/threads.c` can reprogram the timer from non-CPU0 contexts, so the first architectural timer runtime patch would otherwise need to solve CPU-affine wakeup programming and not just timer source selection.
- Splitting timer object selection out first keeps the next code patch reviewable and creates the build hook needed for the eventual common backend without pretending that the wakeup semantics are already solved.

## Source Basis

- `phoenix-rtos-kernel/proc/threads.c`
- `phoenix-rtos-kernel/hal/aarch64/interrupts_gicv2.c`
- `phoenix-rtos-kernel/hal/aarch64/_init.S`
- `phoenix-rtos-kernel/hal/aarch64/zynqmp/timer.c`
- `phoenix-rtos-kernel/hal/timer.h`

## Selected Next Step

- update `phoenix-rtos-kernel/hal/aarch64/Makefile`
- update `phoenix-rtos-kernel/hal/aarch64/zynqmp/Makefile`
- introduce an explicit AArch64 timer-backend object hook while preserving the current ZynqMP timer object selection
- validate with the existing `aarch64a53-zynqmp-qemu` build in `phoenix-dev`
