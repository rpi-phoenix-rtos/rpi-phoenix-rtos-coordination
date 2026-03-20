# Manifest: `dummyfs` Non-Filesystem Stdout-Gate Negative Result

- Date: `2026-03-20`
- Step: `STEP-0131`
- Status: `completed`

## Goal

- determine whether the `dummyfs -N devfs -D` startup path is being held back by the non-filesystem namespace `write(1, "", 0)` gate before it can register `devfs`

## Changes

Experimented locally in:

- `sources/phoenix-rtos-filesystems/dummyfs/srv.c`

The temporary probe removed only the `write(1, "", 0)` retry loop in the `non_fs_namespace` branch before `portCreate()` and `portRegister()`.

The experiment was validated and then reverted because it produced no change.

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

   - still reached:
     - kernel banner
     - `pl011-tty: started`
     - `pl011-tty: register tty0`
     - `pl011-tty: tty0 lookup failed`

2. Pi 4 DTB-backed `raspi4b`

   - still reached:
     - loader startup
     - `pl011-tty: started`
     - `pl011-tty: register tty0`
     - `pl011-tty: tty0 lookup failed`

## Conclusion

- the non-filesystem namespace stdout gate is not the blocker behind the missing `devfs` lookup success
- future agents should not assume that simply removing that wait loop will make `devfs` visible to `pl011-tty`

## Selected Next Step

- measure whether `devfs` becomes available later by adding a bounded retry window around the local raw `lookup("devfs", ...)` helper
