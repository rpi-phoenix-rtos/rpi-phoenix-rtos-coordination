# Pi 4 Deterministic TTBR0 Bootstrap

## Summary

- Date: `2026-04-18`
- Step name: `STEP-0514 Validate the deterministic TTBR0 Pi 4 bootstrap image on real hardware`
- Scope:
  - `phoenix-rtos-kernel/hal/aarch64/_init.S`
  - coordination-repo tracking and reference updates
- Validation lanes used:
  - `./scripts/rebuild-rpi4b-fast.sh --scope core --qemu-sanity`
  - `./scripts/qemu-shell-smoke.sh rpi4b`
  - `/bin/bash /Users/witoldbolt/phoenix-rpi/scripts/qemu-rpi4b-hdmi-smoke.sh`
  - `./scripts/export-rpi4b-sdimg.sh`
  - `./scripts/verify-rpi4b-sdimg.sh`
- Result:
  - the neutral real-board result on the identity-first image was recorded
  - the active kernel fix now replaces the sparse TTBR0 bootstrap map derived
    from `syspage->pkernel` with a deterministic map:
    - first `1 GB` of low physical RAM as normal cacheable memory
    - the `1 GB` block containing `PL011_TTY_BASE` as device memory

## Repositories

| Repository | Remote URL | Branch | Commit SHA | Status |
| --- | --- | --- | --- | --- |
| phoenix-rtos-kernel | `https://github.com/phoenix-rtos/phoenix-rtos-kernel` | `pi4-dev` | `136b4cae` | changed and committed |
| plo | `https://github.com/phoenix-rtos/plo` | `pi4-dev` | unchanged in this step | unchanged |
| phoenix-rtos-devices | `https://github.com/phoenix-rtos/phoenix-rtos-devices` | `pi4-dev` | unchanged in this step | unchanged |
| phoenix-rtos-filesystems | `https://github.com/phoenix-rtos/phoenix-rtos-filesystems` | `pi4-dev` | unchanged in this step | unchanged |
| phoenix-rtos-build | `https://github.com/phoenix-rtos/phoenix-rtos-build` | `pi4-dev` | unchanged in this step | unchanged |
| phoenix-rtos-project | `https://github.com/phoenix-rtos/phoenix-rtos-project` | `pi4-dev` | unchanged in this step | unchanged |
| phoenix-rtos-tests | `https://github.com/phoenix-rtos/phoenix-rtos-tests` | `pi4-dev` | unchanged in this step | unchanged |

## Validation Evidence

- Emulator:
  - `./scripts/rebuild-rpi4b-fast.sh --scope core --qemu-sanity`: pass
  - `./scripts/qemu-shell-smoke.sh rpi4b`: pass
  - `/bin/bash /Users/witoldbolt/phoenix-rpi/scripts/qemu-rpi4b-hdmi-smoke.sh`: pass
- Hardware:
  - previous neutral log on the immediately preceding image:
    `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b-uart/rpi4b-uart-20260418-115137.log`
- UART log location:
  - next expected real-board proof should replace the neutral
    `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b-uart/rpi4b-uart-20260418-115137.log`
- Image or boot-tree location:
  - `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
  - SHA-256 `f44385750b37adc49bb279156e812e561c61ec8d31b983fae457215cd0fab469`

## Notes

- New constraints discovered:
  - the identity-first branch-sequencing change in `phoenix-rtos-kernel 6cd294fd`
    was neutral on real hardware: the board still stopped at `A2 KLM X1 X2 3C`
  - the next stronger candidate fault is therefore the old sparse TTBR0
    bootstrap map itself, not just the moment when TTBR1 becomes active
  - current Pi 4 placements from the committed tree still fit within a simple
    low-memory bootstrap window:
    - preinit DDR map starts at `0x00400000`
    - `loader.disk` is staged at `0x08000000`
    - BCM2711 peripherals remain in the high block containing `0xfe201000`
- Docs updated:
  - `/Users/witoldbolt/phoenix-rpi/docs/status.md`
  - `/Users/witoldbolt/phoenix-rpi/docs/source-artifacts.md`
  - `/Users/witoldbolt/phoenix-rpi/tracking/current-step.md`
  - `/Users/witoldbolt/phoenix-rpi/tracking/step-history.md`
- Next smallest task:
  - flash the new image on real hardware
  - capture UART
  - verify whether the board finally moves beyond the long-standing `3C`
    boundary
