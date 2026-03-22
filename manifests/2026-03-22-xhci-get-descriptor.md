# STEP-0384: Implement the bounded xHCI EP0 `GET_DESCRIPTOR` control-read step

Date: `2026-03-22`

## Goal

Add the first non-`SET_ADDRESS` endpoint-0 transfer path while keeping the
contract explicit and narrow.

## Implemented

In `phoenix-rtos-devices/usb/xhci/xhci.c`:

- added the minimum transfer TRB constants needed for a bounded control-read
  path:
  - setup stage
  - data stage
  - status stage
  - transfer event parsing
- added a small synchronous `xhci_ep0ControlRead()` helper that:
  - builds setup, data, and status TRBs on the existing EP0 ring
  - rings doorbell `1` for the current slot
  - polls the already-programmed event ring for a transfer completion event
  - accepts only `SUCCESS` or `SHORT_PACKET`
  - returns the transferred byte count
- wired the non-roothub transfer path so that, when:
  - the request is `REQ_GET_DESCRIPTOR`
  - the transfer is control IN
  - the request type is standard device-to-host recipient-device
  - the device is the first direct-root-port child
  - the current temporary slot-ID-equals-address contract still holds
  Phoenix will:
  - initialize the EP0 ring
  - execute the bounded control-read helper
  - complete the transfer through `usb_transferFinished()`
- kept the step intentionally narrow:
  - no generic endpoint-0 engine
  - no non-`GET_DESCRIPTOR` requests
  - no non-control endpoint support
  - no live Pi 4 `usb` / `usbkbd` image staging yet

## Validation

- fresh full Pi 4 A72 build in `phoenix-dev`:
  `TARGET=aarch64a72-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image`
- Pi 4 shell non-regression:
  `./scripts/qemu-shell-smoke.sh rpi4b`

## Result

The Pi 4 xHCI path now contains the first real non-`SET_ADDRESS`
endpoint-0 transfer for child-device enumeration. The next concrete blocker is
no longer descriptor reads; it is the first post-enumeration control-write path
used by real drivers such as `usbkbd`, starting with `REQ_SET_CONFIGURATION`.
