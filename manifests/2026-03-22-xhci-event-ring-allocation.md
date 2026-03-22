# STEP-0362

## Title

Implement the smallest xHCI event-ring allocation step

## Date

2026-03-22

## Repositories

- `phoenix-rtos-devices`
- coordination repo

## Change Summary

The Pi 4 xHCI path now allocates the first event-delivery memory objects after
the earlier run-state self-test.

The code now:

- allocates one event-ring segment
- allocates one ERST block
- records the physical addresses of both objects
- derives the event-ring TRB count from the allocated segment size
- populates ERST entry `0` with the event-ring segment base and TRB count

The step intentionally remains:

- pre-runtime-register programming
- pre-interrupt-enable
- pre-doorbell
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
- the staged Pi 4 image composition remained unchanged

## Result

The xHCI path now has the first event-ring memory foundation required before
runtime-register programming:

- event-ring segment
- ERST block
- populated ERST entry for interrupter `0`

## Upstream Commit

- `phoenix-rtos-devices 6a5e8dd`

## Next Step

- scope the smallest xHCI runtime-register programming step for the new event
  ring

