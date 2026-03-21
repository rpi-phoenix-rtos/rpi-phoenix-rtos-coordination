# 2026-03-21: Pi 4 `plo` HDMI visibility via low mailbox buffer

## Scope

- Step: `STEP-0283`
- Goal: implement the first Pi 4 `plo`-side mailbox/framebuffer visibility
  path and validate it under `raspi4b` QEMU

## Repositories Touched

- `plo`
- `phoenix-rtos-project`
- coordination repo

## Key Change

- added a minimal Raspberry Pi mailbox property-call framebuffer path in
  `plo/hal/aarch64/generic/video.c`
- enabled the Pi 4 project to provide:
  - mailbox MMIO base
  - framebuffer geometry
  - a low physical mailbox request buffer at `0x02000000`
- threaded `graphmode` through `plo` generic AArch64 `hal_syspage_t`
- invoked `video_init()` from generic AArch64 `plo` `hal_init()`

## Why The Low Buffer Matters

The first implementation used a regular static `video_mailbox` object inside the
current generic `plo` image. On the Pi 4 A72 lane, that object lived at
`0x4008e300`, above QEMU's `UPPER_RAM_BASE` split (`0x40000000`).

GDB-first validation proved the exact same property request succeeds when the
request buffer is redirected to low physical memory:

- high-buffer request at `0x4008e300`:
  - mailbox request structure is written
  - framebuffer response fields stay unset
  - `video_common.framebuffer` remains zero
- low-buffer GDB bounce at `0x02000000`:
  - response code becomes `0x80000000`
  - framebuffer base returns as `0x3c100000`
  - framebuffer size returns as `0x00300000`
  - pitch returns as `0x00001000`

That made the next source change justified and small:

- keep the mailbox/framebuffer logic
- move only the request buffer into a low Pi 4 board-provided physical address

## Validation

Authoritative build lane:

```sh
./scripts/prepare-buildroot.sh --copy-components /home/witoldbolt.guest/phoenix-buildroots/phoenix-rtos-project-copy
cd /home/witoldbolt.guest/phoenix-buildroots/phoenix-rtos-project-copy
export PATH=/home/witoldbolt.guest/phoenix-toolchains/aarch64-phoenix/bin:$PATH
export RPI4B_DTB_PATH=/home/witoldbolt.guest/external/raspberrypi-firmware/boot/bcm2711-rpi-4-b.dtb
export RPI4B_QEMU_MEMORY_SIZE=80000000
export LIBPHOENIX_DEVEL_MODE=n
TARGET=aarch64a72-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image
```

GDB-first diagnostic proof:

- ran `raspi4b` QEMU under gdbstub
- stopped inside `video_init` at the inlined mailbox-call site
- copied the prepared mailbox request from `0x4008e300` to `0x02000000`
- redirected the active request registers to the low buffer
- observed a valid framebuffer allocation response in the low buffer

Runtime validation after the code fix:

- ran `raspi4b` QEMU with a VNC display backend
- used the QEMU monitor `screendump` command
- confirmed the framebuffer was no longer black

Observed framebuffer signature:

- output PPM size: `1024 x 768`
- top-left marker pixels:
  - `(0, 0) -> (240, 240, 240)`
  - `(20, 20) -> (240, 240, 240)`
  - `(100, 40) -> (240, 240, 240)`
- background pixels:
  - `(200, 120) -> (160, 96, 48)`
  - `(639, 479) -> (160, 96, 48)`
  - `(1023, 767) -> (160, 96, 48)`

## Result

- the Pi 4 `plo` path now allocates a framebuffer under `raspi4b` QEMU
- the first HDMI-visible sign of life is implemented:
  a filled screen with a bright marker rectangle in the upper-left corner
- this is an early `plo` visibility path only; it is not yet a runtime console
  or full graphics subsystem
