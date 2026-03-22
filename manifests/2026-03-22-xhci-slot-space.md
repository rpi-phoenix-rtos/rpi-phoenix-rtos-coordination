# STEP-0378: Implement the first xHCI slot-context allocation and DCBAA binding step

Date: `2026-03-22`

## Goal

Add the smallest per-slot controller-owned memory step after `Enable Slot`,
while staying below `Address Device`.

## Implemented

In `phoenix-rtos-devices/usb/xhci/xhci.c`:

- added minimal 32-byte xHCI context structure definitions for:
  - slot context
  - endpoint context
  - device context
  - input-control context
  - input context
- added bounded per-slot state to `xhci_t` for:
  - one device context
  - one input context
  - one endpoint-0 ring backing block
  - their sizes and physical addresses
- added `xhci_allocSlotSpace()` to:
  - validate the returned `slotId`
  - allocate the device context, input context, and EP0 ring
  - zero the new memory
  - record physical addresses
  - bind the device-context physical address into `DCBAA[slotId]`
- wired the new step into `xhci_init()` immediately after the bounded
  `Enable Slot` command succeeds
- extended `xhci_destroy()` to free the new per-slot objects

## Validation

- fresh full Pi 4 A72 build in `phoenix-dev`:
  `TARGET=aarch64a72-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image`
- Pi 4 QEMU shell smoke still passes on the DTB-prepared image:
  `./scripts/qemu-shell-smoke.sh rpi4b`

## Result

The Pi 4 xHCI path now has the first controller-owned per-slot object needed
for child-device work:

- allocated device context
- allocated input context
- allocated EP0 ring backing block
- non-zero DCBAA binding for the enabled slot

The next blocker is no longer per-slot controller memory. The next bounded
prerequisite is the first minimal `Address Device` context-preparation step:
populate slot/input/EP0 context fields from the Phoenix `usb_dev_t` contract.
