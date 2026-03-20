# Manifest: First AArch64 `gtimer` IRQ Ownership Scope

- Date: `2026-03-20`
- Step: `STEP-0039`
- Result: `completed`

## Scope

- inspect the backend-state helper layer after `STEP-0038`
- choose the smallest IRQ-ownership slice needed before public timer-HAL takeover
- select the exact touched files for that IRQ-helper step

## Result

- selected first IRQ-ownership helper slice:
  backend-state helpers for querying the selected timer IRQ and registering an interrupt handler against it
- selected responsibilities:
  - expose the selected IRQ from backend state
  - register a handler using the selected IRQ without open-coded `state->irq` plumbing at later call sites
  - keep all changes inside `hal/aarch64/gtimer_backend.[ch]`
- selected exact file set:
  - `phoenix-rtos-kernel/hal/aarch64/gtimer_backend.h`
  - `phoenix-rtos-kernel/hal/aarch64/gtimer_backend.c`
- selected validation:
  compile the updated backend layer in the existing `aarch64a53-zynqmp-qemu` build lane in `phoenix-dev`

## Why This Was Selected

- Public `hal_timerIrq()` and `hal_timerRegister()` takeover is now mostly missing IRQ ownership, not timer programming.
- The backend state already owns the selected IRQ, so exposing and registering it centrally avoids repeating raw field plumbing in future public wrappers.
- This keeps the next implementation step small and directly aligned with the remaining public timer-HAL entry points.

## Selected Next Step

- implement backend-state IRQ query and handler-registration helpers in `hal/aarch64/gtimer_backend.[ch]`
