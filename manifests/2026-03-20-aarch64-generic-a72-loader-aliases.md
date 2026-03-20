# Manifest: Generic A72 Loader Alias Groundwork

- Date: `2026-03-20`
- Step: `STEP-0181`
- Status: `completed`

## Goal

- remove the first hard `aarch64a53` generic loader naming assumptions so a future `aarch64a72-generic` target family can exist cleanly

## Upstream Repository

### `plo`

- Commit: `c233c13`

## Changes

Updated:

- `sources/plo/hal/aarch64/generic/config.h`

Added:

- `sources/plo/ld/aarch64a72-generic.ldt`

The generic loader config now:

- selects `phoenix-aarch64a72-generic.elf` when `__TARGET_AARCH64A72` is defined
- includes `ld/aarch64a72-generic.ldt` for that target family
- keeps the current A53 generic names unchanged for existing lanes

The new `aarch64a72-generic.ldt` file currently aliases the existing generic loader layout.

## Validation

Environment:

- `phoenix-dev`
- copied buildroots:
  - `/home/witoldbolt.guest/phoenix-buildroots/phoenix-step0181-generic`
  - `/home/witoldbolt.guest/phoenix-buildroots/phoenix-step0181`

Build validation:

- `LIBPHOENIX_DEVEL_MODE=n TARGET=aarch64a53-generic-qemu ./phoenix-rtos-build/build.sh clean host core project image`
- `LIBPHOENIX_DEVEL_MODE=n RPI4B_DTB_PATH=$HOME/external/raspberrypi-firmware/boot/bcm2711-rpi-4-b.dtb TARGET=aarch64a53-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image`

Both existing A53 generic lanes still build successfully after the change.

## Conclusion

- the first loader-side blocker to adding an A72 generic target family is now removed
- the next bounded step can be the first actual `aarch64a72-generic` target scaffold for Pi 4

## Selected Next Step

- scope the first buildable `aarch64a72-generic-rpi4b` scaffold step
