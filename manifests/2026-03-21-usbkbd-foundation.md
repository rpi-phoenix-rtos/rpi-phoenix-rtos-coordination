# USB HID Boot Keyboard Foundation

Date: `2026-03-21`

## Step

- `STEP-0308` Implement the first reusable USB HID boot-keyboard driver foundation

## Repositories

- `phoenix-rtos-devices` `684468f`
- `phoenix-rtos-build` `9da1732`
- coordination repo

## Summary

- added a new generic USB HID boot-keyboard driver in
  `phoenix-rtos-devices/tty/usbkbd/`
- exposed both an internal host-driver library target
  `libusbdrv-usbkbd` and a process-driver binary target `usbkbd`
- added the first build glue for the existing IA32 EHCI-based USB-host lane
- kept Pi 4 transport explicitly out of scope; this step does not claim BCM2711
  PCIe or VL805 xHCI support

## Key Files

- `sources/phoenix-rtos-devices/tty/usbkbd/usbkbd.c`
- `sources/phoenix-rtos-devices/tty/usbkbd/srv.c`
- `sources/phoenix-rtos-devices/tty/usbkbd/Makefile`
- `sources/phoenix-rtos-devices/_targets/Makefile.ia32-generic`
- `sources/phoenix-rtos-build/build-core-ia32-generic.sh`

## Implementation Notes

- the driver matches USB HID boot keyboards using interface class `0x03`,
  subclass `0x01`, protocol `0x01`
- the first version uses:
  - control pipe for `SET_PROTOCOL` and `SET_IDLE`
  - interrupt-IN URBs for 8-byte boot reports
  - a simple character FIFO behind `/dev/kbdN`
- translation currently focuses on shell-usable cooked input:
  letters, digits, punctuation, enter, tab, backspace, escape, arrows,
  home/end/delete/page navigation
- the current foundation detects only newly pressed keys and does not implement
  host-side autorepeat yet

## Validation

Validated in `phoenix-dev` with the currently available AArch64 toolchain:

```sh
export PATH="$HOME/phoenix-toolchains/aarch64-phoenix/bin:$PATH"
cd /Users/witoldbolt/phoenix-rpi
./scripts/prepare-buildroot.sh --copy-components
cd ~/phoenix-buildroots/phoenix-rtos-project-copy
make -C phoenix-rtos-usb TARGET=aarch64a72-generic-rpi4b libusb usb-headers install
make -C phoenix-rtos-devices TARGET=aarch64a72-generic-rpi4b libusbdrv-usbkbd usbkbd
```

Observed result:

- `libusb`, `libusbdrv-usbkbd`, and `usbkbd` build successfully

## Constraints

- full IA32 `build.sh` validation is currently blocked in `phoenix-dev` because
  the `i386-pc-phoenix-*` toolchain is not installed there yet
- real Pi 4 keyboard interaction still requires later BCM2711 PCIe and VL805
  xHCI support

## Next Logical Step

- bridge `/dev/kbd0` input into `pl011-tty` so that once a USB host transport
  exists, HDMI text console sessions can become interactive without redesigning
  the tty stack
