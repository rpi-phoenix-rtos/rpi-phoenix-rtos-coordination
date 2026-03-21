# Pi 4 xHCI Skeleton

Date: `2026-03-21`

## Step

- `STEP-0330` Implement the first compile-only Pi 4 xHCI HCD skeleton and
  discovery stub

## Repositories

- `phoenix-rtos-devices` `64c95b1`
- `phoenix-rtos-usb` `1210902`
- coordination repo

## Summary

- added the first `libusbxhci` skeleton in `phoenix-rtos-devices`
- added a Pi 4 discovery stub that uses the fixed VL805 fast-path constants
- fixed the first generic AArch64 USB-host build blocker in `phoenix-rtos-usb`:
  port values are now passed through `uintptr_t` instead of lossy `int` casts
- validated that the A72 Pi 4 lane can now compile:
  - `libusbxhci`
  - `libusbdrv-usbkbd`
  - `usbkbd`
  - the Phoenix USB host binary linked against `libusbxhci` and
    `libusbdrv-usbkbd`

## Key Files

- `sources/phoenix-rtos-devices/usb/xhci/Makefile`
- `sources/phoenix-rtos-devices/usb/xhci/xhci.c`
- `sources/phoenix-rtos-devices/usb/xhci/phy-aarch64a72-generic.c`
- `sources/phoenix-rtos-usb/usb/usb.c`

## Validation

Validated in `phoenix-dev`:

```sh
export PATH="$HOME/phoenix-toolchains/aarch64-phoenix/bin:$PATH"
cd /Users/witoldbolt/phoenix-rpi
tmpdir=$(mktemp -d ~/phoenix-buildroots/pi4-xhci-skel.XXXXXX)
./scripts/prepare-buildroot.sh --copy-components "$tmpdir"
cd "$tmpdir"
make -C phoenix-rtos-usb TARGET=aarch64a72-generic-rpi4b libusb usb-headers install
make -C phoenix-rtos-devices TARGET=aarch64a72-generic-rpi4b \
  CPPFLAGS="-I$PWD/_projects/aarch64a72-generic-rpi4b" \
  libusbxhci libusbdrv-usbkbd usbkbd
make -C phoenix-rtos-usb TARGET=aarch64a72-generic-rpi4b \
  CPPFLAGS="-I$PWD/_projects/aarch64a72-generic-rpi4b" \
  usb USB_HCD_LIBS="libusbxhci" USB_HOSTDRV_LIBS="libusbdrv-usbkbd"
```

Observed result:

- `libusbxhci` compiles and archives successfully on the Pi 4 A72 lane
- the Pi 4 `usbkbd` pieces still compile and link successfully
- the Phoenix USB host binary now also compiles and links successfully on the
  Pi 4 A72 lane with `libusbxhci`

## Remaining Gap

- the xHCI library is still a skeleton with `-ENOSYS` runtime behavior
- the A72 default build still does not build Phoenix USB components through its
  normal build-core path
- the live Pi 4 image still does not stage `/sbin/usb` or `usbkbd`

## Next Logical Step

- scope the smallest A72 USB build-glue step for the new xHCI pieces
