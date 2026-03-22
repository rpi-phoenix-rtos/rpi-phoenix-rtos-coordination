# STEP-0383

## Title

Scope the smallest non-`SET_ADDRESS` xHCI endpoint-0 control-transfer step

## Date

2026-03-22

## Outcome

The next bounded xHCI move is now fixed:

- implement the first non-`SET_ADDRESS` endpoint-0 transfer path as a
  synchronous polled control read
- keep it limited to `REQ_GET_DESCRIPTOR`
- keep it limited to the first direct-root-port child device
- keep it below generic control-transfer support, non-control endpoint work,
  and live Pi 4 image staging

## Why This Step

After the bounded `REQ_SET_ADDRESS` path, Phoenix USB enumeration still needs
the first descriptor-read path before it can continue:

- `usb_devEnumerate()` in `phoenix-rtos-usb/usb/dev.c` performs:
  - initial device descriptor read
  - `REQ_SET_ADDRESS`
  - another device descriptor read
  - configuration descriptor reads
- the current xHCI path already has:
  - `Enable Slot`
  - per-slot memory ownership
  - bounded `Address Device`
- but it still lacks the first real endpoint-0 transfer beyond `SET_ADDRESS`

So the smallest useful next seam is not a broad transfer engine; it is a
bounded `GET_DESCRIPTOR` control-read path.

## Next Step

- implement the bounded xHCI EP0 `GET_DESCRIPTOR` control-read step
