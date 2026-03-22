# Current Step

## Metadata

- Step ID: `STEP-0390`
- Title: Implement the bounded xHCI interrupt-IN transfer and no-IRQ completion step
- Status: `in_progress`
- Date: `2026-03-22`
- Milestone / phase: `Phase 1`

## Objective

- implement the smallest real interrupt-transfer seam needed after the new
  endpoint-configuration support

## Scope

In scope:

- queueing exactly one interrupt-IN transfer on the current configured endpoint
- delivering completion by extending the existing no-IRQ xHCI status thread
- keeping the step below generic multi-endpoint scheduling or cancellation

Out of scope:

- generic endpoint-0 transfer support
- implementing the next interrupt-endpoint path yet
- generic multi-endpoint scheduling
- generic transfer cancellation
- staging `/sbin/usb` or `/sbin/usbkbd` on the Pi 4 image

## Expected Repositories

- `phoenix-rtos-devices`
- coordination repo

## Expected Files Or Subsystems

- `sources/phoenix-rtos-devices/usb/xhci/xhci.c`
- `sources/phoenix-rtos-usb/usb/dev.c`
- `sources/phoenix-rtos-usb/libusb/driver.c`
- `sources/phoenix-rtos-devices/tty/usbkbd/usbkbd.c`
- `sources/phoenix-rtos-usb/usb/drv.c`
- `sources/phoenix-rtos-usb/usb/usbhost.h`
- `sources/phoenix-rtos-usb/usb/usb.c`
- `tracking/current-step.md`
- `tracking/step-history.md`
- `docs/status.md`
- `docs/source-artifacts.md`
- `manifests/`

## Acceptance Criteria

- `xhci` can queue exactly one interrupt-IN transfer on the configured endpoint
- the existing no-IRQ status thread can detect the matching transfer event and
  complete the pending transfer
- a fresh full `aarch64a72-generic-rpi4b` build still passes
- the Pi 4 shell smoke still passes because the live image path remains
  unchanged

## Validation Plan

- fresh Pi 4 A72 build in `phoenix-dev` using the standard copied-buildroot
  path
- Pi 4 shell smoke after rebuild, because the live image path remains unchanged

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-22-xhci-interrupt-endpoint.md`

## Notes

- Risks:
  avoid widening the step into generic scheduling, cancellation, or live image
  staging too early
- Dependencies:
  completed `STEP-0389` bounded interrupt-transfer scope
- User-visible control point before next step:
  after this step, the next bounded move should be whichever remaining keyboard
  report seam still blocks live Pi 4 USB staging
