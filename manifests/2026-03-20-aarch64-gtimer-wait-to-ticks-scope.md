# Manifest: First AArch64 `gtimer` Wait-To-Ticks Helper Scope

- Date: `2026-03-20`
- Step: `STEP-0033`
- Result: `completed`

## Scope

- inspect the backend-state helper layer after `STEP-0032`
- choose the smallest forward-conversion helper needed before relative timer programming
- select the exact touched files for that helper step

## Result

- selected first wait-programming helper slice:
  a backend-state helper that converts a relative wait time in microseconds into architectural timer ticks using the state-selected frequency
- selected responsibilities:
  - convert microseconds to relative timer ticks from backend state
  - keep the conversion local to `hal/aarch64/gtimer_backend.[ch]`
  - preserve runtime behavior by not programming timer or control registers yet
- selected exact file set:
  - `phoenix-rtos-kernel/hal/aarch64/gtimer_backend.h`
  - `phoenix-rtos-kernel/hal/aarch64/gtimer_backend.c`
- selected validation:
  compile the updated backend layer in the existing `aarch64a53-zynqmp-qemu` build lane in `phoenix-dev`

## Why This Was Selected

- Future generic `hal_timerSetWakeup()` work needs forward conversion before it needs ownership of timer register programming.
- The backend already exposes the reverse conversion and current-time helpers, so adding the opposite direction keeps the conversion logic symmetric and centralized.
- This keeps the next implementation step purely computational and avoids introducing timer-arming policy too early.

## Selected Next Step

- implement a backend-state microseconds-to-relative-ticks helper in `hal/aarch64/gtimer_backend.[ch]`
