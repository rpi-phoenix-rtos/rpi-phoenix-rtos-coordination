# Manifest: Generic AArch64 Post-Entry A53-Block Visibility

- Date: `2026-03-20`
- Step: `STEP-0179`
- Status: `completed`

## Goal

- determine whether the Pi 4 lane fails before, inside, or after the A53-specific system-register block in generic kernel early init

## Upstream Repository

### `phoenix-rtos-kernel`

- Commit: `ab205d09`

## Changes

Updated:

- `sources/phoenix-rtos-kernel/hal/aarch64/_init.S`

Added two tiny raw markers around the `__TARGET_AARCH64A53` setup block:

- `L` immediately before the block
- `M` immediately after the block

The existing earliest-entry `K` marker remains in place.

## Validation

Environment:

- `phoenix-dev`
- copied buildroots:
  - `/home/witoldbolt.guest/phoenix-buildroots/phoenix-step0179-generic`
  - `/home/witoldbolt.guest/phoenix-buildroots/phoenix-step0179`
- QEMU `10.2.2`
- Pi 4 DTB source:
  - `https://github.com/raspberrypi/firmware`
  - commit `63ad7e7980b030cb4649ecedf2255c9226e5a1e8`
  - `boot/bcm2711-rpi-4-b.dtb`

Build validation:

- `LIBPHOENIX_DEVEL_MODE=n TARGET=aarch64a53-generic-qemu ./phoenix-rtos-build/build.sh clean host core project image`
- `LIBPHOENIX_DEVEL_MODE=n RPI4B_DTB_PATH=$HOME/external/raspberrypi-firmware/boot/bcm2711-rpi-4-b.dtb TARGET=aarch64a53-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image`

Runtime validation:

1. Generic `virt -smp 1`

   - shows:
     - `A3`
     - `KLM`
   - then reaches:
     - `Phoenix-RTOS microkernel v. 3.3.1`
     - later kernel and user-space startup logs

2. Pi 4 `raspi4b -smp 4`

   - shows:
     - `A3`
     - `KLM`
   - still does not reach:
     - `Phoenix-RTOS microkernel v. 3.3.1`

## Conclusion

- the Pi 4 lane gets past the A53-specific system-register block too
- the current Pi 4 failure is therefore later in generic kernel early init
- however, Pi 4 is still a BCM2711 Cortex-A72 platform, so continuing under the `aarch64a53` target identity is no longer the right strategic direction
- the next bounded step should pivot from marker-only debugging to the first Cortex-A72-capable generic target path

## Selected Next Step

- scope the first minimal Cortex-A72-capable generic target path for Pi 4
