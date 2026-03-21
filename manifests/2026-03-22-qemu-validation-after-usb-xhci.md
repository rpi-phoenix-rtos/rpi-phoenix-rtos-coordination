# QEMU Validation After USB/xHCI Changes

## Date

2026-03-22

## Purpose

Record the post-xHCI non-regression validation requested after the recent Pi 4
USB keyboard transport work.

## Repositories

- coordination repo

## Validation Questions

- does generic AArch64 `virt` still boot cleanly after the recent USB/xHCI
  changes?
- does Pi 4 `raspi4b` QEMU still boot cleanly after the recent USB/xHCI
  changes?
- can current QEMU validate any meaningful part of the real Pi 4 USB keyboard
  transport path?

## Validation Commands

Generic shell smoke:

```sh
./scripts/qemu-shell-smoke.sh generic
```

Pi 4 DTB preparation:

```sh
cpp -nostdinc \
  -I external/raspberrypi-linux/include \
  -I external/raspberrypi-linux/arch/arm/boot/dts \
  -I external/raspberrypi-linux/arch/arm/boot/dts/broadcom \
  -undef -x assembler-with-cpp \
  external/raspberrypi-linux/arch/arm/boot/dts/broadcom/bcm2711-rpi-4-b.dts \
  >/tmp/rpi4b-dtb/bcm2711-rpi-4-b.pre.dts

dtc -I dts -O dtb \
  -o /tmp/rpi4b-dtb/bcm2711-rpi-4-b.dtb \
  /tmp/rpi4b-dtb/bcm2711-rpi-4-b.pre.dts
```

Pi 4 image rebuild for QEMU:

```sh
./scripts/prepare-buildroot.sh --copy-components
cd ~/phoenix-buildroots/phoenix-rtos-project-copy
export PATH="$HOME/phoenix-toolchains/aarch64-phoenix/bin:$PATH"
export RPI4B_DTB_PATH=/tmp/rpi4b-dtb/bcm2711-rpi-4-b.dtb
export RPI4B_QEMU_MEMORY_SIZE=80000000
TARGET=aarch64a72-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image
```

Pi 4 shell smoke:

```sh
./scripts/qemu-shell-smoke.sh rpi4b
```

Pi 4 HDMI smoke:

```sh
/bin/bash /Users/witoldbolt/phoenix-rpi/scripts/qemu-rpi4b-hdmi-smoke.sh
```

## Results

Generic `virt`:

- shell smoke passed
- `help` round-trip completed
- `(psh)%` prompt returned

Pi 4 `raspi4b`:

- the first rerun on an image without the explicit DTB-prepared build was a
  false negative and stopped after early `plo` handoff
- after rebuilding the Pi 4 image with:
  - `RPI4B_DTB_PATH`
  - `RPI4B_QEMU_MEMORY_SIZE=80000000`
  the Pi 4 shell smoke passed
- the Pi 4 HDMI smoke also passed on that same rebuilt image

Observed passing Pi 4 shell markers:

- `main: Starting syspage programs: ... 'pcie', 'psh'`
- `dummyfs: initialized`
- `psh: tty ready`
- `(psh)%`
- `Available commands:`

Observed passing Pi 4 HDMI markers:

- framebuffer `1024x768`
- text-row white pixel count above the current threshold
- black background preserved in the sampled area

## USB-Specific Conclusion

QEMU currently validates only non-regression of boot and later userspace
startup after the USB/xHCI source changes.

It does not validate the real Pi 4 USB keyboard transport path yet, because the
current `raspi4b` machine does not expose the BCM2711 PCIe root-port path
needed for VL805 xHCI bring-up.

## Debugging Notes

- GDB was not needed for this validation pass
- the initial Pi 4 failure was not a proven runtime regression in the new USB
  work
- it was a bad test artifact caused by rerunning the Pi 4 smoke on an image
  that had not been rebuilt with the required DTB and memory-node preparation

## Next Step

- continue `STEP-0354` xHCI controller register programming
