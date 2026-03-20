# Manifest: First AArch64 `gtimer` Arming Helper Scope

- Date: `2026-03-20`
- Step: `STEP-0037`
- Result: `completed`

## Scope

- inspect the backend-state helper layer after `STEP-0036`
- choose the smallest timer-arming policy slice needed before public timer integration
- select the exact touched files for that arming-helper step

## Result

- selected first timer-arming policy slice:
  a backend-state helper that takes a relative wait in microseconds, converts it to timer ticks, clamps positive waits away from zero ticks, programs the selected timer register, and enables the selected timer unmasked
- selected responsibilities:
  - keep the arming policy local to `hal/aarch64/gtimer_backend.[ch]`
  - use the existing backend conversion and register-wrapper helpers
  - avoid public `hal_timerSetWakeup()` integration and avoid IRQ registration in the same step
- selected exact file set:
  - `phoenix-rtos-kernel/hal/aarch64/gtimer_backend.h`
  - `phoenix-rtos-kernel/hal/aarch64/gtimer_backend.c`
- selected validation:
  compile the updated backend layer in the existing `aarch64a53-zynqmp-qemu` build lane in `phoenix-dev`

## Why This Was Selected

- The scheduler already computes bounded relative wakeups, so the next missing backend capability is the actual arming sequence for those waits.
- The backend now already owns conversion helpers and state-keyed register wrappers, so the arming logic can stay compact and readable.
- This keeps the next step policy-focused while still stopping short of public HAL takeover and interrupt registration.

## Selected Next Step

- implement a backend-state relative timer-arming helper in `hal/aarch64/gtimer_backend.[ch]`
