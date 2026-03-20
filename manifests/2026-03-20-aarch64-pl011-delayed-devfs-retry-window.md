# Manifest: Bounded `pl011-tty` `devfs` Retry Window

- Date: `2026-03-20`
- Step: `STEP-0132`
- Status: `completed`

## Goal

- determine whether `devfs` becomes available shortly after the first failed `/dev/tty0` lookup attempt or whether a later lookup path blocks instead

## Upstream Repository

### `phoenix-rtos-devices`

- Commit: `37d10bd`

## Changes

Updated:

- `sources/phoenix-rtos-devices/tty/pl011-tty/pl011-tty.c`

Extended only the local raw `pl011_createTty0()` helper to:

- retry `lookup("devfs", ...)` up to 50 times
- sleep `100 ms` between failed attempts
- emit raw UART retry markers on the first retry and every tenth retry

The helper still remains diagnostic and bounded.

## Validation

Environment:

- `phoenix-dev`
- copied buildroots:
  - `/home/witoldbolt.guest/phoenix-buildroots/phoenix-step0132-generic`
  - `/home/witoldbolt.guest/phoenix-buildroots/phoenix-step0132`
- QEMU `10.2.2` for both runtime lanes

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
     - `pl011-tty: tty0 lookup retry`
   - did not reach before timeout:
     - `pl011-tty: tty0 lookup ok`
     - `pl011-tty: tty0 lookup failed`
     - `pl011-tty: tty0 send`

2. Pi 4 DTB-backed `raspi4b`

   - reached:
     - loader startup
     - `pl011-tty: started`
     - `pl011-tty: register tty0`
     - `pl011-tty: tty0 lookup`
     - `pl011-tty: tty0 lookup retry`
   - did not reach before timeout:
     - `pl011-tty: tty0 lookup ok`
     - `pl011-tty: tty0 lookup failed`
     - `pl011-tty: tty0 send`

## Conclusion

- `devfs` does not simply appear within a short delay window after the first failed lookup
- a later `lookup("devfs", ...)` call is now blocking before it can return success or failure
- the next bounded blocker is therefore whether the `dummyfs` `devfs` instance reaches its main loop and responds to the lookup request

## Selected Next Step

- add narrow kernel-console `debug()` markers to `dummyfs` around non-filesystem registration, post-init, and the first `mtLookup` handling path
