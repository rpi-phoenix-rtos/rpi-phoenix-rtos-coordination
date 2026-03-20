# Manifest: `dummyfs` Non-Filesystem Startup Visibility

- Date: `2026-03-20`
- Step: `STEP-0134`
- Status: `completed`

## Goal

- determine whether the non-filesystem `devfs` dummyfs instance reaches registration and main-loop readiness before the stalled later `lookup("devfs", ...)` path

## Upstream Repository

### `phoenix-rtos-filesystems`

- Commit: `ea03ccc`

## Changes

Updated:

- `sources/phoenix-rtos-filesystems/dummyfs/srv.c`

Added bounded `debug()` markers for the non-filesystem dummyfs instance:

- after successful `portRegister()` in the `-N` path
- after the existing `initialized` boundary
- around the first `mtLookup` receive / response path

## Validation

Environment:

- `phoenix-dev`
- copied buildroots:
  - `/home/witoldbolt.guest/phoenix-buildroots/phoenix-step0134-generic`
  - `/home/witoldbolt.guest/phoenix-buildroots/phoenix-step0134`
- QEMU `10.2.2` for both runtime lanes
- note: non-interactive VM builds required explicit `PATH="$HOME/phoenix-toolchains/aarch64-phoenix/bin:$PATH"` export before `build.sh`

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
     - `dummyfs: nonfs registered`
     - `dummyfs: initialized`
   - did not reach before timeout:
     - `dummyfs: lookup recv`
     - `dummyfs: lookup rsp`
     - `pl011-tty: tty0 lookup ok`

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

- on the generic lane, the non-filesystem `devfs` instance starts late but does reach its initialized main loop after the first retry marker
- the blocked later `lookup("devfs", ...)` path still never reaches that non-filesystem instance
- the next bounded target is therefore the root dummyfs lookup path, because early unresolved `devfs` lookups still flow through root before `devfs` is cached

## Selected Next Step

- extend the same bounded marker set to distinguish root dummyfs initialization and first `mtLookup` handling from the later non-filesystem `devfs` startup
