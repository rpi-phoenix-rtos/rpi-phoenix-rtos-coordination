# Manifest: `create_dev()` Stdout Probe Negative Result

- Date: `2026-03-20`
- Step: `STEP-0129`
- Status: `completed`

## Goal

- determine whether a bounded stdout-visible probe inside `libphoenix/create_dev()` can expose the tiny post-lookup gap that the kernel-side markers left unresolved

## Changes

Experimented locally in:

- `sources/libphoenix/unistd/file.c`

The temporary probe used bounded fd-1 writes around the same narrow `create_dev()` points that had already been inspected with `debug()`:

- `lookup("devfs", ...)`
- fallback `/dev` handling
- the final device-node create path

The probe was validated and then reverted because it produced no visible new signal.

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
   - did not reach:
     - any new stdout-visible `create_dev()` marker

2. Pi 4 DTB-backed `raspi4b`

   - reached:
     - loader startup
     - `pl011-tty: started`
     - `pl011-tty: register tty0`
   - did not reach:
     - any new stdout-visible `create_dev()` marker

## Conclusion

- plain fd-1 writes are not a useful early visibility path for this boot window
- future agents should not assume that a stdout-side `create_dev()` probe will be observable before `/dev/tty0` exists

## Selected Next Step

- replace only the first `/dev/tty0` `create_dev()` call with a driver-local raw helper so the live boundary can be observed without depending on shared early-boot I/O paths
