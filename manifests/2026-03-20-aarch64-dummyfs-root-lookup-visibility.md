# Manifest: Root-Versus-`devfs` `dummyfs` Visibility Result

- Date: `2026-03-20`
- Step: `STEP-0136`
- Status: `completed`

## Goal

- determine whether the blocked later `lookup("devfs", ...)` call is waiting on a root dummyfs instance rather than on the later non-filesystem `devfs` instance

## Upstream Repository

### `phoenix-rtos-filesystems`

- Commit: `f29321a`

## Changes

Updated:

- `sources/phoenix-rtos-filesystems/dummyfs/srv.c`

Refined the bounded marker set so it distinguishes:

- `dummyfs: devfs registered`
- `dummyfs: devfs initialized`
- `dummyfs: root initialized`
- `dummyfs: root lookup recv`
- `dummyfs: root lookup rsp`

The root lookup markers are filtered to `mtLookup` requests for the literal name `devfs`.

## Validation

Environment:

- `phoenix-dev`
- copied buildroots:
  - `/home/witoldbolt.guest/phoenix-buildroots/phoenix-step0136-generic`
  - `/home/witoldbolt.guest/phoenix-buildroots/phoenix-step0136`
- QEMU `10.2.2` for both runtime lanes
- non-interactive VM builds again required explicit `PATH="$HOME/phoenix-toolchains/aarch64-phoenix/bin:$PATH"` export

Build validation:

- `LIBPHOENIX_DEVEL_MODE=n TARGET=aarch64a53-generic-qemu ./phoenix-rtos-build/build.sh clean host core project image`
- `LIBPHOENIX_DEVEL_MODE=n RPI4B_DTB_PATH=$HOME/phoenix-buildroots/phoenix-step0132/_boot/aarch64a53-generic-rpi4b/rpi4b/bcm2711-rpi-4-b.dtb TARGET=aarch64a53-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image`

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
     - `dummyfs: devfs registered`
     - `dummyfs: devfs initialized`
   - did not reach before timeout:
     - `dummyfs: root initialized`
     - `dummyfs: root lookup recv`
     - `dummyfs: root lookup rsp`

2. Pi 4 DTB-backed `raspi4b`

   - reached:
     - loader startup
     - `pl011-tty: started`
     - `pl011-tty: register tty0`
     - `pl011-tty: tty0 lookup`
     - `pl011-tty: tty0 lookup retry`
   - did not reach before timeout:
     - any visible `dummyfs:` marker

## Conclusion

- the later visible startup markers are definitively coming from the non-filesystem `devfs` instance
- the current generic / Pi 4 fast-lane images do not produce any evidence of a root dummyfs instance
- the root-dummyfs hypothesis is therefore invalid for the current image layout
- the next bounded target is the kernel name-service layer, which owns `/` registration state and `lookup("devfs", ...)` branch selection

## Selected Next Step

- instrument `proc/name.c` for `/` and `devfs` state transitions so the generic lane can distinguish no-root failure, cached success, and root-mediated blocking
