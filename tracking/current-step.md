# Current Step

## Metadata

- Step ID: `STEP-0354`
- Title: Implement the smallest xHCI register-programming step for `DCBAA`, `CRCR`, and `CONFIG`
- Status: `in_progress`
- Date: `2026-03-22`
- Milestone / phase: `Phase 1`

## Objective

- implement the next bounded xHCI register-programming step after the new
  command-space allocation baseline without widening into full host-controller
  operation

## Scope

In scope:

- extracting:
  - `DCBAAP` programming
  - `CRCR` programming
  - `CONFIG` programming
- validating only the smallest controller-setup constraints needed before later
  run-state or event-ring work
- preserving the current compile-valid, unstaged USB-host baseline on the Pi 4

Out of scope:

- SD-image export or checksum refresh
- manual hardware execution
- staged runtime USB host support
- command/event ring setup
- interrupt setup
- root-hub modeling or enumeration
- changes to the existing `usbkbd` or `pl011-tty` logic
- broad BCM2711 PCIe host-bridge redesign
- actual DCBAA programming
- actual command-ring programming
- doorbell writes
- interrupter programming
- `RUN/STOP` transition
- device enumeration

## Expected Repositories

- coordination repo
- `phoenix-rtos-devices`

## Expected Files Or Subsystems

- `sources/phoenix-rtos-devices/usb/xhci/`
- `sources/phoenix-rtos-usb/usb/mem.c`
- `external/circle/include/circle/usb/xhci.h`
- `docs/status.md`
- `docs/source-artifacts.md`
- `tracking/current-step.md`
- `tracking/step-history.md`
- `manifests/`

## Acceptance Criteria

- `xhci` programs the first controller memory-layout registers
- the new checks stay pre-run, pre-doorbell, pre-interrupt-enable, and
  pre-enumeration
- the full `aarch64a72-generic-rpi4b` build remains clean with the live staged
  Pi 4 image unchanged

## Validation Plan

- fresh `aarch64a72-generic-rpi4b` build from the copied VM-local buildroot in
  `phoenix-dev`

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-22-xhci-command-space.md`

## Notes

- Risks:
  do not widen the step into `RUN/STOP`, doorbell writes, interrupter work, or
  root-port logic just because the controller memory objects now exist
- Dependencies:
  completed `STEP-0353` xHCI register-programming scope
- User-visible control point before next step:
  after this step lands, `xhci` should bind the first command-space objects to
  the controller, but should still make no host-operation claim
