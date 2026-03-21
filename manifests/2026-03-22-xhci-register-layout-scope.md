# STEP-0343

## Title

Scope the smallest xHCI controller-register layout refinement

## Date

2026-03-22

## Outcome

The next bounded xHCI move is now fixed:

- extract the controller-level doorbell and runtime-register offsets from the
  capability block
- validate only the smallest layout constraints justified before later
  interrupter or ring work
- keep the step pre-root-hub, pre-interrupt, pre-ring, and pre-enumeration

## Why This Step

After the controller-shape facts are known, the next structural dependency for
future xHCI work is the placement of the doorbell and runtime register spaces.
Those offsets are needed before any future interrupter or command/event-ring
step can be designed coherently, but they can still be captured without
claiming operational USB-host behavior.

## Reference

- `external/circle/include/circle/usb/xhci.h`

## Validation Plan

- fresh copied-buildroot Pi 4 A72 build in `phoenix-dev`

## Next Step

- implement bounded `DBOFF` and `RTSOFF` extraction and validation in
  `usb/xhci/xhci.c`
