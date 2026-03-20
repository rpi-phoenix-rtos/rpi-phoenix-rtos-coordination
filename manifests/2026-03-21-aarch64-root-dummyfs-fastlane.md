# Manifest: Root Dummyfs Fast-Lane Image Change

- Date: `2026-03-21`
- Step: `STEP-0241`
- Status: `completed`

## Goal

- make `/` exist on the shared generic and Pi 4 fast lanes with the smallest
  image-shape change

## Implementation

Changed repository:

- `sources/phoenix-rtos-project`

Changed files:

- `sources/phoenix-rtos-project/_projects/aarch64a53-generic-qemu/build.project`
- `sources/phoenix-rtos-project/_projects/aarch64a53-generic-qemu/user.plo.yaml`
- `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/build.project`
- `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/user.plo.yaml`

Upstream commit:

- `phoenix-rtos-project 1be31fd`

Bounded change:

- staged the existing `dummyfs` binary a second time under the distinct boot
  alias `dummyfs-root`
- switched the root filesystem instance to `app ... dummyfs-root ...`
- kept the existing `dummyfs;-N;devfs;-D` instance unchanged

Why the extra alias is necessary:

- `phoenix-rtos-project/_targets/build.common:b_mkscript_user()` aliases each
  `app` payload by `basename(path)`
- two `app ... dummyfs` entries therefore collide in the generated `user.plo`
  script and abort in `plo` with `alias is already in use`

## Validation

### Build guardrails

- refreshed the copied buildroot with:
  `./scripts/prepare-buildroot.sh --copy-components`
- generic build:
  `TARGET=aarch64a53-generic-qemu ./phoenix-rtos-build/build.sh clean host core project image`
- Pi 4 build:
  `RPI4B_DTB_PATH=.../bcm2711-rpi-4-b.dtb RPI4B_QEMU_MEMORY_SIZE=80000000 TARGET=aarch64a72-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image`
- both succeeded in `phoenix-dev`

Validation note:

- generic and Pi 4 builds were run sequentially against the copied buildroot

### Generated script verification

Both generated `user.plo` scripts now contain distinct aliases:

- `alias -r dummyfs-root ...`
- `app ram0 -x dummyfs-root ...`
- `alias -r dummyfs ...`
- `app ram0 -x dummyfs;-N;devfs;-D ...`

### Runtime verification

#### Generic `virt`

Within the 30-second QEMU window, the lane now advances past the old rootfs
stall and prints:

- `name: register /`
- `dummyfs: root initialized`
- `syscalls: psh root lookup 0`
- `psh: root ready`
- `psh: app run`
- `psh: run enter`
- `psh: tty open`
- `psh: app done`

#### Pi 4 `raspi4b`

Within the same 30-second QEMU window, the Pi 4 lane now matches the same
rootfs recovery and prints:

- `name: register /`
- `dummyfs: root initialized`
- `syscalls: psh root lookup 0`
- `psh: root ready`
- `psh: app run`
- `psh: run enter`
- `psh: tty open`
- `psh: app done`

## Result

- the shared blocker has moved forward exactly as intended
- `/` is now present on both fast lanes
- the next live boundary is inside `psh_ttyopen(console)`, before
  `psh: tty ready`

## Next Step

- scope the smallest visibility step that distinguishes why
  `psh_ttyopen("/dev/console")` fails on the shared fast lane
