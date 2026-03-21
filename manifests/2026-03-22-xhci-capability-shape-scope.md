# STEP-0341

## Title

Scope the smallest next xHCI structural-capability refinement

## Date

2026-03-22

## Outcome

The next bounded xHCI move is now fixed:

- extract the next early capability fields needed before any root-hub, ring, or
  interrupt work
- keep the step limited to:
  - max slots
  - max scratchpad buffers
  - context size
- keep the result compile-validated only on the Pi 4 A72 lane

## Why This Step

After page-size and port-count validation, the next practical controller-shape
questions are whether the controller reports a usable slot count, whether it
exposes scratchpad-buffer requirements, and whether it expects 32-byte or
64-byte contexts. Those facts directly constrain later ring and device-context
work, but they do not require widening into operational host-controller logic
yet.

## Validation Plan

- fresh copied-buildroot Pi 4 A72 build in `phoenix-dev`

## Next Step

- implement the bounded xHCI capability-shape refinement in
  `usb/xhci/xhci.c`
