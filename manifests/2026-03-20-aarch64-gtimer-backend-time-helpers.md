# Manifest: AArch64 `gtimer` Backend Time Helpers

- Date: `2026-03-20`
- Step: `STEP-0032`
- Result: `completed`

## Scope

- update `hal/aarch64/gtimer_backend.h`
- update `hal/aarch64/gtimer_backend.c`
- add backend-state helpers for raw count reads and microsecond conversion
- preserve the existing ZynqMP timer backend and validate the existing `aarch64a53-zynqmp-qemu` build in `phoenix-dev`

## Touched Repositories

- `phoenix-rtos-kernel`
- coordination repo

## Upstream Commits

- `phoenix-rtos-kernel`: `c926564c` (`aarch64: add gtimer backend time helpers`)

## Validation

- Refreshed the copied buildroot in `phoenix-dev`:
  `./scripts/prepare-buildroot.sh --copy-components`
- Rebuilt the existing AArch64 QEMU lane in `phoenix-dev`:
  `TARGET=aarch64a53-zynqmp-qemu ./phoenix-rtos-build/build.sh clean host core project`
- Build result: success

## Key Findings

- The backend-state layer now exposes current-count and current-time helpers without taking over the public `hal_timer*` entry points.
- Time conversion is now centralized on the state-selected timer frequency, which will keep future wakeup-programming steps from repeating raw counter math.
- The next backend slice can focus on the missing forward conversion needed for relative wakeup programming instead of reopening current-time access.

## Selected Next Step

- define the first backend wait-to-ticks helper step on top of the new time helpers
