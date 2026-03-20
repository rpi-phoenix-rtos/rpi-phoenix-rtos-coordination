# Manifest: First Buildable `aarch64a72-generic-rpi4b` Scaffold

- Date: `2026-03-20`
- Step: `STEP-0183`
- Status: `completed`

## Goal

- add the first buildable Cortex-A72-capable generic Pi 4 target scaffold without widening into runtime behavior changes

## Upstream Repositories

### `phoenix-rtos-build`

- Commit: `f05f148`

### `phoenix-rtos-project`

- Commit: `002d561`

### `phoenix-rtos-filesystems`

- Commit: `468bea5`

### `phoenix-rtos-devices`

- Commit: `fcb651c`

### `phoenix-rtos-utils`

- Commit: `f76e2c7`

## Changes

Updated:

- `sources/phoenix-rtos-build/makes/include-target.mk`

Added:

- `sources/phoenix-rtos-build/build-core-aarch64a72-generic.sh`
- `sources/phoenix-rtos-project/_targets/aarch64a72/generic/build.project`
- `sources/phoenix-rtos-project/_targets/aarch64a72/generic/nvm.yaml`
- `sources/phoenix-rtos-project/_targets/aarch64a72/generic/preinit.plo.yaml`
- `sources/phoenix-rtos-project/_targets/aarch64a72/generic/user.plo.yaml`
- `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/board_config.h`
- `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/build.project`
- `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/config.txt`
- `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/user.plo.yaml`
- `sources/phoenix-rtos-filesystems/_targets/Makefile.aarch64a72-generic`
- `sources/phoenix-rtos-devices/_targets/Makefile.aarch64a72-generic`
- `sources/phoenix-rtos-utils/_targets/Makefile.aarch64a72-generic`

## Validation

Environment:

- `phoenix-dev`
- copied buildroots under `/home/witoldbolt.guest/phoenix-buildroots`

Build validation:

- `LIBPHOENIX_DEVEL_MODE=n TARGET=aarch64a53-generic-qemu ./phoenix-rtos-build/build.sh clean host core project image`
- `LIBPHOENIX_DEVEL_MODE=n RPI4B_DTB_PATH=$HOME/external/raspberrypi-firmware/boot/bcm2711-rpi-4-b.dtb TARGET=aarch64a53-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image`
- `LIBPHOENIX_DEVEL_MODE=n RPI4B_DTB_PATH=$HOME/external/raspberrypi-firmware/boot/bcm2711-rpi-4-b.dtb TARGET=aarch64a72-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image`

All three builds completed successfully after the scaffold landed.

## Conclusion

- the first buildable A72-capable Pi 4 target scaffold now exists locally
- the existing generic A53 diagnostic lanes are still intact
- the next bounded step should be the first runtime validation of the A72 Pi 4 lane under `qemu-system-aarch64 -M raspi4b`

## Selected Next Step

- scope the first runtime validation of `aarch64a72-generic-rpi4b` on the Pi 4 QEMU lane with the official firmware DTB
