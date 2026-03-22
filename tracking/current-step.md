# Current Step

## Metadata

- Step ID: `STEP-0385`
- Title: Scope the smallest xHCI post-enumeration control-write step
- Status: `in_progress`
- Date: `2026-03-22`
- Milestone / phase: `Phase 1`

## Objective

- choose the smallest real post-enumeration control-write seam needed by
  `usbkbd` after the new bounded descriptor-read path

## Scope

In scope:

- deciding which first non-roothub control write should be implemented next
- keeping the next move as narrow as possible
- using the existing Phoenix USB enumeration and `usbkbd` call paths to justify
  the chosen seam

Out of scope:

- implementing the next control-write path yet
- generic endpoint-0 transfer support
- interrupt-IN endpoint work
- staging `/sbin/usb` or `/sbin/usbkbd` on the Pi 4 image

## Expected Repositories

- `phoenix-rtos-devices`
- coordination repo

## Expected Files Or Subsystems

- `sources/phoenix-rtos-devices/usb/xhci/xhci.c`
- `sources/phoenix-rtos-usb/usb/dev.c`
- `sources/phoenix-rtos-usb/libusb/driver.c`
- `sources/phoenix-rtos-devices/tty/usbkbd/usbkbd.c`
- `tracking/current-step.md`
- `tracking/step-history.md`
- `docs/status.md`
- `docs/source-artifacts.md`
- `manifests/`

## Acceptance Criteria

- the next post-`GET_DESCRIPTOR` seam is explicitly chosen and documented
- the choice is justified against the actual Phoenix USB and `usbkbd` call
  paths
- the chosen next step stays narrow and still below live Pi 4 image staging

## Validation Plan

- code reading and bounded source-path analysis only

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-22-xhci-get-descriptor.md`

## Notes

- Risks:
  avoid widening the next move into a generic control-transfer engine too early
- Dependencies:
  completed `STEP-0384` bounded `REQ_GET_DESCRIPTOR` control-read support
- User-visible control point before next step:
  after this scope step, the next bounded move should be the first real control
  write actually required by the existing Phoenix USB host and `usbkbd` flow
