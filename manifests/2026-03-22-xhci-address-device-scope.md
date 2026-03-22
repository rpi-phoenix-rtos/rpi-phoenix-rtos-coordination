# STEP-0377: Scope the smallest xHCI `Address Device` step

Date: `2026-03-22`

## Goal

Choose the first bounded controller-owned object needed after `Enable Slot`,
without widening into full endpoint-0 transfers or broad enumeration.

## Decision

The smallest correct next seam is not the `Address Device` command itself.
The first prerequisite is a bounded per-slot controller object for the newly
enabled slot:

- one device context
- one input context
- one endpoint-0 transfer-ring backing block
- one DCBAA binding for the returned slot ID

This keeps the next implementation step below:

- `Address Device` command emission
- slot-context population from a live `usb_dev_t`
- endpoint-0 control transfers
- broad enumeration or `/sbin/usb` image staging

## Rationale

- `Enable Slot` only returns a slot ID; Phoenix still has no controller-owned
  memory for the child device behind that slot.
- `Address Device` needs at least:
  - a DCBAA entry for the slot
  - a device context
  - an input context
  - an endpoint-0 ring pointer contract
- Choosing this seam first avoids jumping directly into a large mixed step that
  combines allocation, context population, command submission, and control
  transfer logic.

## Validation

- source review of the current Phoenix xHCI code in
  `phoenix-rtos-devices/usb/xhci/xhci.c`
- source review of the current Phoenix USB enumeration path in
  `phoenix-rtos-usb/usb/dev.c`
- reference review of Circle's per-slot and `Address Device` path in:
  - `external/circle/lib/usb/xhcislotmanager.cpp`
  - `external/circle/lib/usb/xhciusbdevice.cpp`

## Next step

Implement the first xHCI slot-context allocation and DCBAA binding step.
