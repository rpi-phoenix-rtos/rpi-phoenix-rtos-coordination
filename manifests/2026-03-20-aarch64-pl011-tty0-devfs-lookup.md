# Manifest: Local `pl011-tty` `/dev/tty0` `devfs` Lookup Diagnostic

- Date: `2026-03-20`
- Step: `STEP-0130`
- Status: `completed`

## Goal

- determine whether the first `/dev/tty0` registration attempt fails in the initial `lookup("devfs", ...)` call or later in the create-message path

## Upstream Repository

### `phoenix-rtos-devices`

- Commit: `f9d2e22`

## Changes

Updated:

- `sources/phoenix-rtos-devices/tty/pl011-tty/pl011-tty.c`

Replaced only the first `/dev/tty0` registration call with a local raw UART-side helper that:

- emits explicit progress markers before and after `lookup("devfs", ...)`
- performs the `mtCreate` message directly for `tty0`
- preserves the rest of the driver startup flow unchanged

## Validation

Environment:

- `phoenix-dev`
- copied buildroots for:
  - generic `aarch64a53-generic-qemu`
  - Pi 4 DTB-backed `aarch64a53-generic-rpi4b`

Build validation:

- `TARGET=aarch64a53-generic-qemu ./phoenix-rtos-build/build.sh clean host core project image`
- `RPI4B_DTB_PATH=... TARGET=aarch64a53-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image`

Runtime validation:

1. Generic `virt`

   - reached:
     - kernel banner
     - `pl011-tty: started`
     - `pl011-tty: register tty0`
     - `pl011-tty: tty0 lookup`
     - `create_dev: lookup devfs`
     - `create_dev: lookup done`
     - `pl011-tty: tty0 lookup failed`
     - `pl011-tty: tty0 failed`

2. Pi 4 DTB-backed `raspi4b`

   - reached:
     - loader startup
     - `pl011-tty: started`
     - `pl011-tty: register tty0`
     - `pl011-tty: tty0 lookup`
     - `pl011-tty: tty0 lookup failed`
     - `pl011-tty: tty0 failed`

## Conclusion

- the first `lookup("devfs", ...)` returns a failure quickly on both QEMU lanes at the time of the first `/dev/tty0` registration attempt
- the current blocker is therefore earlier than the final `mtCreate` message for `tty0`

## Selected Next Step

- test whether the `dummyfs -N devfs -D` startup path is still being gated by the non-filesystem namespace stdout wait
