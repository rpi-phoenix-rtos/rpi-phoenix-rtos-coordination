# STEP-0345

## Title

Scope the smallest pre-interrupt xHCI capability refinement

## Date

2026-03-22

## Outcome

The next bounded xHCI move is now fixed:

- extract the remaining pre-interrupt capability fields needed before any later
  event-ring or interrupter step:
  - max interrupters
  - interrupt moderation interval scale
  - maximum event-ring segment table size
- keep the step read-only and structural
- keep the result pre-interrupt-enable, pre-ring-allocation, and
  pre-enumeration

## Why This Step

After `DBOFF` and `RTSOFF` are known, the next still-safe facts are the
controller limits around interrupter layout and event-ring sizing. Those values
shape later event-ring design, but they can still be collected without
claiming runtime host-controller operation.

## Reference

- `external/circle/include/circle/usb/xhci.h`

## Validation Plan

- fresh copied-buildroot Pi 4 A72 build in `phoenix-dev`

## Next Step

- implement the bounded pre-interrupt capability extraction in
  `usb/xhci/xhci.c`
