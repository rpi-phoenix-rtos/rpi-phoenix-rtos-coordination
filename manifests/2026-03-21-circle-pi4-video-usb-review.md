# 2026-03-21: review Circle for Pi 4 video and USB keyboard bring-up guidance

## Scope

- Step: `STEP-0295`
- Goal: analyze Circle as a concrete implementation reference for Raspberry Pi 4
  HDMI output and USB keyboard support, then capture implementation-relevant
  guidance in the Phoenix knowledge base

## Repositories Touched

- coordination repo

## Sources Reviewed

Framebuffer and mailbox path:

- `external/circle/lib/bcmmailbox.cpp`
- `external/circle/lib/bcmpropertytags.cpp`
- `external/circle/lib/bcmframebuffer.cpp`
- `external/circle/lib/screen.cpp`
- `external/circle/include/circle/bcmmailbox.h`
- `external/circle/include/circle/bcmpropertytags.h`
- `external/circle/include/circle/bcmframebuffer.h`
- `external/circle/include/circle/screen.h`
- `external/circle/sample/02-screenpixel/kernel.cpp`
- `external/circle/sample/03-screentext/kernel.cpp`

USB keyboard path:

- `external/circle/include/circle/usb/usbhcidevice.h`
- `external/circle/include/circle/usb/xhcidevice.h`
- `external/circle/include/circle/bcmpciehostbridge.h`
- `external/circle/include/circle/usb/usbkeyboard.h`
- `external/circle/include/circle/usb/usbhid.h`
- `external/circle/include/circle/input/keyboardbehaviour.h`
- `external/circle/include/circle/input/keyboardbuffer.h`
- `external/circle/lib/usb/xhcidevice.cpp`
- `external/circle/lib/usb/usbdevicefactory.cpp`
- `external/circle/lib/usb/usbkeyboard.cpp`
- `external/circle/lib/input/keyboardbehaviour.cpp`
- `external/circle/lib/input/keyboardbuffer.cpp`
- `external/circle/sample/08-usbkeyboard/kernel.cpp`
- `external/circle/sample/08-usbkeyboard/kernel.h`

## Findings

### 1. Early video

Circle strongly validates the current Phoenix direction:

- use the Raspberry Pi property mailbox for early Pi 4 framebuffer allocation
- keep the property buffer in coherent low memory
- use explicit barriers around submission and completion
- treat framebuffer allocation as a small firmware contract, not a full display
  driver

It also shows two useful later refinements:

- Pi 4 display selection through `GET_NUM_DISPLAYS` and `SET_DISPLAY_NUM`
- a clean layering from raw framebuffer to text-console rendering

### 2. USB keyboard

Circle proves that Pi 4 USB keyboard support is not a small early shortcut.
Its Pi 4 path is:

- PCIe host bridge
- VL805 xHCI enablement and reset notification
- USB enumeration and device factory matching
- HID keyboard transport
- cooked keymap and console behavior

So for Phoenix, USB keyboard should remain a later milestone behind PCIe and
xHCI, not the next pre-boot or first-board-test task.

### 3. Licensing and style

Circle is GPL and C++ while Phoenix is not following the same design or style.
Its value here is as a behavioral reference only:

- hardware sequence
- subsystem layering
- fallback policy
- interrupt-context rules

not as copyable source.

## Knowledge Base Updates

The review is captured in:

- `docs/circle-reference-review.md`
- `docs/source-artifacts.md`
- `docs/platforms/raspberry-pi-4.md`
- `docs/status.md`

## Result

- Circle is now explicitly recorded as a high-value reference for:
  - Pi 4 mailbox/property framebuffer behavior
  - later Pi 4 PCIe/xHCI/USB keyboard work
- the next bounded technical move should stay in the HDMI visibility path for
  the current no-UART real-board lab

