# STEP-0334: Implement the first runtime-safe xHCI initialization slice

## Date

- 2026-03-21

## Repositories And SHAs

- `phoenix-rtos-devices`: `dd6a9fa`

## Change Summary

- implemented the first runtime-safe Pi 4 xHCI init slice in
  `sources/phoenix-rtos-devices/usb/xhci/xhci.c`
- added:
  - a small xHCI private state structure
  - MMIO mapping for the fixed Pi 4 xHCI window
  - capability-header reads for:
    - `CAPLENGTH`
    - `HCIVERSION`
    - `HCSPARAMS1`
  - validation for capability-length range and the expected Pi 4 xHCI version
- kept the controller intentionally non-operational:
  `xhci_init()` still returns failure after a successful probe, so the Phoenix
  USB host stack will not attempt enumeration yet

## Validation

- refreshed the copied VM-local buildroot with:
  `./scripts/prepare-buildroot.sh --copy-components`
- built a fresh Pi 4 A72 image in `phoenix-dev` with:
  `TARGET=aarch64a72-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image`
- result:
  build passed, and the staged Pi 4 program set remained unchanged

## Result

- the xHCI path is no longer a pure compile-only `-ENOSYS` stub
- the next missing runtime slice is now explicit:
  controller reset and operational-register readiness before any enumeration
  attempt
