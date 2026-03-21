# Pi 4 xHCI Skeleton Scope

Date: `2026-03-21`

## Step

- `STEP-0329` Scope the first compile-only Pi 4 xHCI HCD skeleton and
  discovery stub

## Repositories

- coordination repo

## Summary

- reviewed the smallest path from the new VL805 fast-path constants to actual
  xHCI code
- selected a compile-only first step:
  - one `libusbxhci` skeleton library
  - one Pi 4 discovery stub using the fixed VL805 contract
  - no staging into the live Pi 4 image path yet
- explicitly kept runtime USB host startup out of scope for this step

## Design Notes

- the current Phoenix USB host stack can already consume an HCD library plus
  host-side drivers
- the current Pi 4 boot path should not yet stage `/sbin/usb`, because
  `hcd_init()` treats missing or failing HCD initialization as fatal
- the smallest safe first move is therefore compile-valid xHCI code only

## Validation

- source inspection against:
  - `sources/phoenix-rtos-usb/usb/hcd.c`
  - `sources/phoenix-rtos-usb/usb/usb.c`
  - `sources/phoenix-rtos-devices/usb/ehci/`
  - the new Pi 4 VL805 board constants

## Next Logical Step

- implement the compile-only Pi 4 xHCI HCD skeleton and discovery stub

