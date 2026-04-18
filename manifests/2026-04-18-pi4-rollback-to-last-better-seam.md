# Pi 4 Rollback To Last Better MMU Seam

## Summary

- Date: `2026-04-18`
- Step name: `STEP-0515 Validate the rollback to the last objectively better Pi 4 MMU seam`
- Scope:
  - `phoenix-rtos-kernel/hal/aarch64/_init.S`
  - `phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/board_config.h`
  - coordination-repo tracking updates
- Validation lanes used:
  - `./scripts/rebuild-rpi4b-fast.sh --scope core --qemu-sanity`
  - `./scripts/qemu-shell-smoke.sh rpi4b`
  - `/bin/bash /Users/witoldbolt/phoenix-rpi/scripts/qemu-rpi4b-hdmi-smoke.sh`
  - `./scripts/export-rpi4b-sdimg.sh`
  - `./scripts/verify-rpi4b-sdimg.sh`
- Result:
  - after two consecutive real-board MMU experiments stayed stuck at `3C`,
    the early-kernel path was rolled back to the last objectively better
    hardware seam instead of continuing forward from a weaker baseline

## Repositories

| Repository | Remote URL | Branch | Commit SHA | Status |
| --- | --- | --- | --- | --- |
| phoenix-rtos-kernel | `https://github.com/phoenix-rtos/phoenix-rtos-kernel` | `pi4-dev` | `91f5f9d5` | changed and committed |
| plo | `https://github.com/phoenix-rtos/plo` | `pi4-dev` | unchanged in this step | unchanged |
| phoenix-rtos-devices | `https://github.com/phoenix-rtos/phoenix-rtos-devices` | `pi4-dev` | unchanged in this step | unchanged |
| phoenix-rtos-filesystems | `https://github.com/phoenix-rtos/phoenix-rtos-filesystems` | `pi4-dev` | unchanged in this step | unchanged |
| phoenix-rtos-build | `https://github.com/phoenix-rtos/phoenix-rtos-build` | `pi4-dev` | unchanged in this step | unchanged |
| phoenix-rtos-project | `https://github.com/phoenix-rtos/phoenix-rtos-project` | `pi4-dev` | `e8f794f` | changed and committed |
| phoenix-rtos-tests | `https://github.com/phoenix-rtos/phoenix-rtos-tests` | `pi4-dev` | unchanged in this step | unchanged |

## Validation Evidence

- Emulator:
  - `./scripts/rebuild-rpi4b-fast.sh --scope core --qemu-sanity`: pass
  - `./scripts/qemu-shell-smoke.sh rpi4b`: pass
  - `/bin/bash /Users/witoldbolt/phoenix-rpi/scripts/qemu-rpi4b-hdmi-smoke.sh`: pass
- Hardware:
  - the immediate previous real-board retries proving the weaker baseline:
    - `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b-uart/rpi4b-uart-20260418-115137.log`
    - `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b-uart/rpi4b-uart-20260418-220352.log`
- UART log location:
  - next expected proof should check whether the board returns to the earlier
    better seam seen in:
    - `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b-uart/rpi4b-uart-20260417-213826.log`
    - `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b-uart/rpi4b-uart-20260417-215745.log`
- Image or boot-tree location:
  - `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
  - SHA-256 `be8c2773306870a5b66b75f64677d68d0a344f01ee348d2e1598aea969ca4fb1`

## Notes

- New constraints discovered:
  - the first real-board retry on `6cd294fd` stayed at `A2 KLM X1 X2 3C`
  - the first real-board retry on `136b4cae` also stayed at
    `A2 KLM X1 X2 3C`
  - the last objectively better seam in the tracker remains the older
    `A2 KLM X1 X2 X3 NO` state
- Rollback policy used here:
  - restore only the early-kernel `_init.S` path to the `c0fd7ff7` lineage
  - restore only the matching `PL011_TTY_EARLY_VADDR` board-config define from
    `5218c40`
  - keep later independent DTB and TLB fixes outside those files
- Docs updated:
  - `/Users/witoldbolt/phoenix-rpi/docs/status.md`
  - `/Users/witoldbolt/phoenix-rpi/tracking/current-step.md`
  - `/Users/witoldbolt/phoenix-rpi/tracking/step-history.md`
- Next smallest task:
  - flash the rollback image
  - capture UART
  - verify whether the board returns to `... X3NO`
