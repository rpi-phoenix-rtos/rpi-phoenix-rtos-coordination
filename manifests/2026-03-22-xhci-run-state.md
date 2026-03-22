# STEP-0360

## Title

Implement the smallest xHCI run-state self-test beyond command-space binding

## Date

2026-03-22

## Repositories

- `phoenix-rtos-devices`
- coordination repo

## Change Summary

The Pi 4 xHCI path now performs the first bounded operational-state validation
after the earlier command-space setup.

The code now:

- checks that the controller is halted after reset
- rejects immediate host/system error state before entering run
- sets `RUN/STOP`
- waits for `HCHalted` to clear
- checks again for immediate host/system error state
- clears `RUN/STOP`
- waits for `HCHalted` to return
- checks once more for immediate host/system error state

The step still intentionally remains:

- pre-event-ring
- pre-interrupt-enable
- pre-doorbell
- pre-root-hub
- pre-enumeration

and `xhci_init()` still returns `-ENOSYS` after the self-test.

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

The xHCI path now proves more than passive register programming:
it can validate the first controller transition from halted to running and back
to halted while still refusing to claim a usable USB host.

## Upstream Commit

- `phoenix-rtos-devices 07fbf65`

## Next Step

- scope the smallest xHCI event-ring allocation step
