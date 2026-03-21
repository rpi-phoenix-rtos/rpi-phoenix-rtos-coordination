# AArch64 A72 USB Build Glue

Date: `2026-03-21`

## Step

- `STEP-0332` Implement the smallest A72 USB build-glue step for the new xHCI
  pieces

## Repositories

- `phoenix-rtos-build` `962da50`
- `phoenix-rtos-devices` `65e4334`
- coordination repo

## Summary

- the normal `aarch64a72-generic` build now builds:
  - `libusbxhci`
  - `libusbdrv-usbkbd`
  - `usbkbd`
  - the Phoenix USB host binary linked against `libusbxhci` and
    `libusbdrv-usbkbd`
- the A72 device target now includes the new USB pieces in its default device
  component set
- the Pi 4 image script still does not stage `/sbin/usb` or `usbkbd`, so this
  step improves build readiness without changing the live boot path

## Key Files

- `sources/phoenix-rtos-build/build-core-aarch64a72-generic.sh`
- `sources/phoenix-rtos-devices/_targets/Makefile.aarch64a72-generic`

## Validation

Validated in `phoenix-dev`:

```sh
export PATH="$HOME/phoenix-toolchains/aarch64-phoenix/bin:$PATH"
cd /Users/witoldbolt/phoenix-rpi
tmpdir=$(mktemp -d ~/phoenix-buildroots/pi4-usb-glue.XXXXXX)
./scripts/prepare-buildroot.sh --copy-components "$tmpdir"
cd "$tmpdir"
TARGET=aarch64a72-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image
```

Observed result:

- the full Pi 4 A72 build still succeeds from a fresh disposable buildroot
- the normal build now produces:
  - `_fs/aarch64a72-generic-rpi4b/root/sbin/usb`
  - `_fs/aarch64a72-generic-rpi4b/root/sbin/usbkbd`
- the live Pi 4 staged program list is unchanged and still does not include
  `usb` or `usbkbd`

## Remaining Gap

- the xHCI library still returns `-ENOSYS` at runtime
- the Pi 4 image still does not stage the USB host path
- no real-device keyboard enumeration has been attempted yet

## Next Logical Step

- scope the smallest first runtime-safe xHCI initialization slice
