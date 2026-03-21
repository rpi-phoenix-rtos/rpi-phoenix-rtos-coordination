# AArch64 A72 USB Build Glue Scope

Date: `2026-03-21`

## Step

- `STEP-0331` Scope the smallest A72 USB build-glue step for the new xHCI
  pieces

## Repositories

- coordination repo

## Summary

- reviewed the remaining gap between the new compile-valid xHCI pieces and the
  normal `aarch64a72-generic` build path
- selected the smallest build-glue slice:
  - add the new USB pieces to the A72 default device components
  - teach the A72 core build to build `phoenix-rtos-usb`
  - keep `/sbin/usb` and `usbkbd` out of the live Pi 4 `user.plo` script

## Design Notes

- this step is intentionally build-only:
  it makes the A72 Pi 4 build produce the USB host and keyboard artifacts, but
  it does not yet risk boot-time failure by staging them
- this follows the existing IA32 pattern structurally, while keeping the HCD
  and host-driver selection Pi 4-specific

## Validation

- source inspection against:
  - `sources/phoenix-rtos-build/build-core-aarch64a72-generic.sh`
  - `sources/phoenix-rtos-devices/_targets/Makefile.aarch64a72-generic`
  - the new compile-valid `libusbxhci` and `usbkbd` pieces

## Next Logical Step

- implement the smallest A72 USB build-glue step for the new xHCI pieces

