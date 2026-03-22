# Current Step

## Metadata

- Step ID: `STEP-0386`
- Title: Implement the bounded xHCI EP0 control-write/no-data path
- Status: `in_progress`
- Date: `2026-03-22`
- Milestone / phase: `Phase 1`

## Objective

- implement the smallest real post-enumeration control-write seam needed by
  `usbkbd` after the new bounded descriptor-read path

## Scope

In scope:

- adding the minimum TRB shape and polling needed for endpoint-0 OUT requests
  with no data stage
- handling only the current direct-root-port child under the temporary
  slot-ID-equals-address contract
- supporting only the currently required post-enumeration writes:
  `REQ_SET_CONFIGURATION`, `CLASS_REQ_SET_PROTOCOL`, and
  `CLASS_REQ_SET_IDLE`

Out of scope:

- generic endpoint-0 transfer support
- control transfers with a data stage
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

- `xhci` can execute a bounded synchronous EP0 control write with setup and
  status TRBs only
- the path stays limited to the current direct-root-port child and to the
  required zero-length OUT requests
- a fresh full `aarch64a72-generic-rpi4b` build still passes
- the Pi 4 shell smoke still passes because the live image path remains
  unchanged

## Validation Plan

- fresh Pi 4 A72 build in `phoenix-dev` using the standard copied-buildroot
  path
- Pi 4 shell smoke after rebuild, because the live image path remains unchanged

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-22-xhci-get-descriptor.md`

## Notes

- Risks:
  avoid widening the step into a generic endpoint-0 engine too early
- Dependencies:
  completed `STEP-0385` bounded post-enumeration control-write scope
- User-visible control point before next step:
  after this step, the next bounded move should be whichever endpoint or
  transfer shape still blocks a real keyboard report path
