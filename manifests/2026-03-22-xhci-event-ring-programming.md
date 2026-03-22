# STEP-0364

## Title

Implement the smallest xHCI runtime-register programming step for the new event ring

## Date

2026-03-22

## Repositories

- `phoenix-rtos-devices`
- coordination repo

## Change Summary

The Pi 4 xHCI path now binds the already allocated event-ring state into the
runtime register block for interrupter `0`.

The code now:

- programs `ERSTSZ`
- programs `ERSTBA`
- programs `ERDP`
- reads the same registers back
- verifies that the read-back values match the allocated ERST and event-ring
  physical addresses
- checks that `EHB` is still clear in the programmed `ERDP` view

The step intentionally remains:

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

The xHCI path now exposes a controller-visible event-ring foundation:

- event-ring memory exists
- ERST memory exists
- runtime interrupter `0` points at that state

without yet enabling interrupts or attempting any command submission.

## Upstream Commit

- `phoenix-rtos-devices e550dbb`

## Next Step

- scope the smallest xHCI command-ring layout initialization step

