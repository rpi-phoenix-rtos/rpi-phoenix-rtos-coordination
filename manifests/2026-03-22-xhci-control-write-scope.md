# STEP-0385

## Title

Scope the smallest xHCI post-enumeration control-write step

## Date

2026-03-22

## Outcome

The next bounded xHCI move is now fixed:

- add one bounded endpoint-0 control-write/no-data helper
- use it for the first direct-root-port child only
- keep it limited to the current post-enumeration writes already required by
  Phoenix:
  - `REQ_SET_CONFIGURATION`
  - HID boot-keyboard `CLASS_REQ_SET_PROTOCOL`
  - HID boot-keyboard `CLASS_REQ_SET_IDLE`

## Why This Step

The new bounded `REQ_GET_DESCRIPTOR` path is enough for enumeration to keep
reading descriptors, but it is not enough for the existing in-tree driver path:

- `usbkbd_handleInsertion()` in `phoenix-rtos-devices/tty/usbkbd/usbkbd.c`
  does:
  - `usb_setConfiguration()`
  - `usb_open(... interrupt, in)`
  - `CLASS_REQ_SET_PROTOCOL`
  - `CLASS_REQ_SET_IDLE`
- all three control writes are zero-length OUT requests on endpoint 0
- implementing only `REQ_SET_CONFIGURATION` would immediately dead-end on the
  next two HID writes

So the smallest useful next seam is a bounded control-write/no-data path, not a
single-request special case and not a generic control engine.

## Next Step

- implement the bounded xHCI EP0 control-write/no-data path
