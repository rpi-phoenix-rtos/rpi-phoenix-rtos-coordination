# STEP-0353

## Title

Scope the smallest xHCI register-programming step for `DCBAA`, `CRCR`, and `CONFIG`

## Date

2026-03-22

## Outcome

The next bounded xHCI move is now fixed:

- write the first controller configuration registers needed to make the
  allocated command space meaningful:
  - `DCBAAP`
  - `CRCR`
  - `CONFIG`
- keep the step pre-run, pre-doorbell, pre-interrupt-enable, and
  pre-enumeration
- keep the result limited to controller setup rather than full host operation

## Why This Step

After the capability block, operational layout, and the first controller-owned
memory objects are all in place, the next clean seam is the first register
programming that binds those allocations to the controller. That is the first
step where xHCI stops being purely passive structure discovery.

## Validation Plan

- fresh copied-buildroot Pi 4 A72 build in `phoenix-dev`

## Next Step

- implement bounded programming of `DCBAAP`, `CRCR`, and `CONFIG` in
  `usb/xhci/xhci.c`
