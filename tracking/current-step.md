# Current Step

## Metadata

- Step ID: `STEP-0388`
- Title: Implement the bounded xHCI interrupt-IN endpoint ownership/configuration step
- Status: `in_progress`
- Date: `2026-03-22`
- Milestone / phase: `Phase 1`

## Objective

- implement the smallest real interrupt-endpoint seam needed after the new
  bounded control-transfer support

## Scope

In scope:

- adding the minimum state needed for one interrupt-IN endpoint on the current
  direct-root-port child
- deriving endpoint identity from the current `usb_pipe_t`
- allocating one transfer ring and populating one endpoint context
- issuing one bounded `Configure Endpoint` command

Out of scope:

- generic endpoint-0 transfer support
- implementing the next interrupt-endpoint path yet
- interrupt transfer submission or completion delivery
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
- `tracking/current-step.md`
- `tracking/step-history.md`
- `docs/status.md`
- `docs/source-artifacts.md`
- `manifests/`

## Acceptance Criteria

- `xhci` can allocate and remember one interrupt-IN endpoint ring for the
  current child device
- `xhci` can populate the matching endpoint context and complete one bounded
  `Configure Endpoint` command
- a fresh full `aarch64a72-generic-rpi4b` build still passes
- the Pi 4 shell smoke still passes because the live image path remains
  unchanged

## Validation Plan

- fresh Pi 4 A72 build in `phoenix-dev` using the standard copied-buildroot
  path
- Pi 4 shell smoke after rebuild, because the live image path remains unchanged

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-22-xhci-control-write.md`

## Notes

- Risks:
  avoid widening the step into interrupt transfer submission or generic
  multi-endpoint support too early
- Dependencies:
  completed `STEP-0387` bounded interrupt-endpoint scope
- User-visible control point before next step:
  after this step, the next bounded move should be whichever transfer-submission
  or completion-delivery seam still blocks keyboard reports
