# STEP-0333: Scope the first runtime-safe xHCI initialization slice

## Date

- 2026-03-21

## Goal

- define the smallest xHCI runtime step that should follow the new A72 USB
  build glue without staging `/sbin/usb` into the live Pi 4 image

## Inputs Reviewed

- `sources/phoenix-rtos-devices/usb/xhci/xhci.c`
- `sources/phoenix-rtos-devices/usb/xhci/phy-aarch64a72-generic.c`
- `sources/phoenix-rtos-usb/usb/hcd.c`
- `sources/phoenix-rtos-devices/usb/ehci/phy-ia32-generic.c`
- `external/circle/include/circle/usb/xhci.h`
- `external/circle/lib/usb/xhcidevice.cpp`

## Scope Decision

- the first runtime-safe xHCI step should not try to enumerate devices or stage
  `/sbin/usb` into the live Pi 4 image
- the first runtime-safe step should:
  - map the fixed Pi 4 MMIO window
  - read the xHCI capability header
  - validate the controller version and capability-length seam needed by later
    reset and operational-register work
  - fail cleanly before enumeration

## Why This Is The Smallest Safe Step

- `phoenix-rtos-usb/usb/hcd.c` treats `ops->init()` failure as non-fatal for the
  overall host stack, but it will still attempt root-hub enumeration on any
  controller that reports success
- the current xHCI code has no root-hub, ring, interrupt, or reset logic yet,
  so returning success would be misleading and unsafe
- Pi 4 `raspi4b` QEMU still cannot validate the real PCIe plus VL805 path, so
  the next step should maximize compile-time confidence while keeping runtime
  behavior explicit and narrow

## Deferred To Later Steps

- xHCI controller reset
- page-size validation
- command/event ring setup
- interrupt setup
- root-hub modeling and device enumeration
- staging `/sbin/usb` into the live Pi 4 image
