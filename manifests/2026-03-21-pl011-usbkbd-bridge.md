# PL011 USB Keyboard Bridge

Date: `2026-03-21`

## Step

- `STEP-0310` Implement the smallest `/dev/kbd0` to `pl011-tty` bridge

## Repositories

- `phoenix-rtos-devices` `f70b1db`
- `phoenix-rtos-project` `e61f067`
- coordination repo

## Summary

- added a small optional keyboard-reader thread to `pl011-tty`
- the bridge opens `/dev/kbd0`, reads cooked bytes, and injects them into the
  existing `libtty` receive path with `libtty_putchar_unlocked()`
- enabled that bridge only for the Pi 4 A72 project through board config

## Key Files

- `sources/phoenix-rtos-devices/tty/pl011-tty/pl011-tty.c`
- `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/board_config.h`

## Design Notes

- the bridge intentionally avoids tty-stack redesign:
  UART RX, USB-keyboard RX, shell line discipline, echo, UART TX, and HDMI text
  output all still flow through the same `libtty` state
- the bridge retries quietly until `/dev/kbd0` exists, so it does not require
  USB host transport to be present at boot
- the current design keeps all Pi 4 transport work explicitly out of scope

## Validation

Validated in `phoenix-dev` with the currently available AArch64 toolchain:

```sh
export PATH="$HOME/phoenix-toolchains/aarch64-phoenix/bin:$PATH"
cd /Users/witoldbolt/phoenix-rpi
./scripts/prepare-buildroot.sh --copy-components
cd ~/phoenix-buildroots/phoenix-rtos-project-copy
TARGET=aarch64a72-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image
make -C phoenix-rtos-usb TARGET=aarch64a72-generic-rpi4b libusb usb-headers install
make -C phoenix-rtos-devices TARGET=aarch64a72-generic-rpi4b libusbdrv-usbkbd usbkbd
```

Observed result:

- Pi 4 A72 project build succeeds with the `pl011-tty` bridge enabled
- `usbkbd` still compiles successfully in the same buildroot

## Remaining Gap

- real Pi 4 USB keyboard interaction is still blocked by missing BCM2711 PCIe
  and VL805 xHCI support

## Next Logical Step

- scope the first Pi 4 USB transport milestone that can eventually surface a
  real `/dev/kbd0` on the board
