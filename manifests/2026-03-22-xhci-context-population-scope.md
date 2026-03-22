# STEP-0379: Scope the smallest xHCI context-population step before `Address Device`

Date: `2026-03-22`

## Goal

Choose the minimum set of context fields Phoenix must prepare after slot-memory
allocation but before issuing a bounded `Address Device` command.

## Decision

The smallest correct next seam is:

- initialize the EP0 ring as a real ring with a final link TRB and initial
  cycle-state contract
- populate only the minimum `Address Device` input-context subset derived from
  the Phoenix `usb_dev_t` contract:
  - input-control `AddContextFlags`
  - slot-context root-hub port, speed, and context entries
  - EP0 dequeue pointer, error count, endpoint type, average TRB length, and
    max-packet size

This step should stay below:

- the `Address Device` command itself
- generic non-roothub control transfers
- broad per-device state management beyond the single current slot object

## Rationale

- Phoenix already records enough device-side information for the first direct
  root-hub child:
  - `usb_dev_t->port`
  - `usb_dev_t->speed`
- `Address Device` cannot proceed until the input context holds a valid slot
  contract and a valid EP0 dequeue pointer.
- EP0 ring layout should be initialized in the same step because the dequeue
  pointer should reference a real ring rather than only a zeroed backing block.

## Validation

- source review of:
  - `phoenix-rtos-usb/usb/dev.h`
  - `phoenix-rtos-devices/usb/xhci/xhci.c`
- reference review of:
  - `external/circle/lib/usb/xhciusbdevice.cpp`
  - `external/circle/include/circle/usb/xhciconfig.h`

## Next step

Implement the smallest xHCI context-population step before `Address Device`.
