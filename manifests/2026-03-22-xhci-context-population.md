# STEP-0380: Implement the smallest xHCI context-population step before `Address Device`

Date: `2026-03-22`

## Goal

Prepare the minimum slot/input/EP0 context state Phoenix needs after slot
memory ownership and before a bounded `Address Device` command.

## Implemented

In `phoenix-rtos-devices/usb/xhci/xhci.c`:

- added bounded helpers for the first direct-root-port child path:
  - USB-speed to xHCI PSI translation
  - EP0 max-packet-size selection
- turned the EP0 backing block into a real ring layout with:
  - initial cycle-state contract
  - final link TRB
- added the first bounded `Address Device` context-preparation helper that:
  - clears the input context
  - sets `AddContextFlags` for slot and EP0
  - populates the minimal slot-context fields from `usb_dev_t`
    - root-hub port
    - speed
    - context entries
  - populates the minimal EP0 context fields from `usb_dev_t`
    - dequeue pointer
    - error count
    - endpoint type
    - max packet size
    - average TRB length
- wired that helper into the non-roothub address-0 enqueue path, but still
  returns `-ENOSYS` before any `Address Device` command or endpoint-0 transfer

## Validation

- fresh full Pi 4 A72 build in `phoenix-dev`:
  `TARGET=aarch64a72-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image`
- attempted Pi 4 shell smoke after rebuild:
  - the harness log reached the echoed `help` command
  - the wrapper did not produce a conclusive prompt/`Available commands`
    completion in this run
  - because the live image still does not stage `/sbin/usb`, that inconclusive
    shell-smoke rerun was not treated as the gating lane for this xHCI-only
    step

## Result

The Pi 4 xHCI path now has the first prepared `Address Device` input-context
state for a direct root-hub child, but still lacks the command-side policy to
translate Phoenix enumeration into xHCI device addressing.

## Next blocker

The next blocker is no longer context structure preparation. The next concrete
decision is the address-ownership contract:

- Phoenix currently requests `REQ_SET_ADDRESS` with an HCD-allocated USB
  address
- xHCI `Address Device` is anchored on a slot ID

That contract needs to be scoped explicitly before implementing the first
bounded `Address Device` command path.
