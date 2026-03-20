# Manifest: `pl011-tty` Registration Diagnostics

- Date: `2026-03-20`
- Step: `STEP-0125`
- Status: `completed`

## Goal

- determine where `pl011-tty` stops between startup and `/dev/tty0` registration on the generic and Pi 4 DTB-backed QEMU lanes

## Upstream Repository

### `phoenix-rtos-devices`

- Commit: `c2855c2`

## Changes

Updated:

- `sources/phoenix-rtos-devices/tty/pl011-tty/pl011-tty.c`

Added raw UART-side markers:

- before `/dev/tty0` registration
- before `/dev/console` registration
- on `/dev/tty0` failure
- on `/dev/console` failure

## Validation

Environment:

- `phoenix-dev`
- copied buildroot:
  - `/home/witoldbolt.guest/phoenix-buildroots/phoenix-step0125-pl011-regdiag`

Build validation:

- `TARGET=aarch64a53-generic-qemu ./phoenix-rtos-build/build.sh clean host core project image`
- `RPI4B_DTB_PATH=... TARGET=aarch64a53-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image`

Runtime validation:

1. Generic `virt`

   - reached:
     - kernel banner
     - `pl011-tty: started`
     - `pl011-tty: register tty0`
   - did not reach:
     - `pl011-tty: tty0 failed`
     - `pl011-tty: tty0 ready`

2. Pi 4 DTB-backed `raspi4b`

   - reached:
     - loader startup
     - `pl011-tty: started`
     - `pl011-tty: register tty0`
   - did not reach:
     - `pl011-tty: tty0 failed`
     - `pl011-tty: tty0 ready`

## Conclusion

- both lanes now stop inside or below the first `create_dev("/dev/tty0")` call
- the current boundary is no longer in `pl011_init()` or before registration starts
- the next smallest useful step should instrument the shared device-registration path rather than `pl011-tty` itself

## Selected Next Step

- scope the smallest `create_dev()`-side diagnostic step:
  - trace `devfs` lookup progress
  - distinguish lookup retry from create-message blocking
  - keep the next patch reviewable and local if possible
