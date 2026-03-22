# STEP-0354

## Title

Implement the smallest xHCI register-programming step for `DCBAAP`, `CRCR`, and `CONFIG`

## Date

2026-03-22

## Repositories

- `phoenix-rtos-devices`
- coordination repo

## Change Summary

The Pi 4 xHCI path now performs the first bounded controller register-programming
step after the earlier capability discovery and command-space allocation work.

The code now:

- writes `DCBAAP` from the allocated `dcbaaPhys`
- writes `CRCR` from the allocated `cmdRingPhys`
- sets the command-ring cycle-state bit in `CRCR`
- writes `CONFIG` with the discovered controller slot count
- reads the same registers back and verifies the programmed state

The step intentionally stays:

- pre-`RUN/STOP`
- pre-doorbell
- pre-interrupt-enable
- pre-event-ring
- pre-root-hub
- pre-enumeration

## Files

- `sources/phoenix-rtos-devices/usb/xhci/xhci.c`

## Validation

Validated in `phoenix-dev` with a fresh copied-buildroot Pi 4 A72 build:

```sh
./scripts/prepare-buildroot.sh --copy-components
cd ~/phoenix-buildroots/phoenix-rtos-project-copy
export PATH="$HOME/phoenix-toolchains/aarch64-phoenix/bin:$PATH"
export RPI4B_DTB_PATH=/tmp/rpi4b-dtb/bcm2711-rpi-4-b.dtb
export RPI4B_QEMU_MEMORY_SIZE=80000000
TARGET=aarch64a72-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image
```

Result:

- build completed successfully
- the live staged Pi 4 image composition remained unchanged

Additional post-build validation:

- `./scripts/qemu-shell-smoke.sh generic` still passed
- `/bin/bash /Users/witoldbolt/phoenix-rpi/scripts/qemu-rpi4b-hdmi-smoke.sh`
  still passed
- the Pi 4 `rpi4b` shell smoke exposed a separate runtime issue outside this
  compile-only xHCI step, which is scoped in
  `manifests/2026-03-22-pi4-console-open-race-scope.md`

## Result

The xHCI path now binds the first controller-owned memory objects to the
controller through:

- `DCBAAP`
- `CRCR`
- `CONFIG`

without yet claiming that the controller is running or enumerating devices.

## Upstream Commit

- `phoenix-rtos-devices 617f0c2`

## Next Step

- scope the smallest Pi 4 shell-side fix for the current `/dev/console`
  startup race before resuming deeper USB-host runtime work
