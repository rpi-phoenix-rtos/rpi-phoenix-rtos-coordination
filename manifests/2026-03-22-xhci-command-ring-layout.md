# STEP-0366

## Title

Implement the smallest xHCI command-ring layout initialization step

## Date

2026-03-22

## Repositories

- `phoenix-rtos-devices`
- coordination repo

## Change Summary

The Pi 4 xHCI path now turns the allocated command-ring backing memory into a
minimal valid xHCI ring layout.

The code now:

- derives the command-ring TRB count from the allocated backing size
- records the initial command-ring cycle state
- treats the backing memory as TRBs
- initializes the final TRB as a link TRB that points back to the first TRB
- verifies the link target, TRB type, and initial cycle/toggle bits

The step intentionally remains:

- pre-doorbell
- pre-command submission
- pre-interrupt-enable
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

The xHCI path now has both of the core memory-side prerequisites for a later
polled or interrupt-driven command path:

- a real command ring
- a controller-visible event ring

## Upstream Commit

- `phoenix-rtos-devices 7fc6420`

## Next Step

- scope the smallest polled xHCI command-submission step

