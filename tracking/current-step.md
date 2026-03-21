# Current Step

## Metadata

- Step ID: `STEP-0346`
- Title: Implement the smallest pre-interrupt xHCI capability refinement
- Status: `in_progress`
- Date: `2026-03-22`
- Milestone / phase: `Phase 1`

## Objective

- implement the next bounded pre-interrupt xHCI capability step after the new
  register-layout data without widening into operational controller logic

## Scope

In scope:

- extracting:
  - max interrupters
  - interrupt moderation interval scale
  - maximum event-ring segment table size
- validating only the smallest layout constraints needed before later
  event-ring or interrupter work
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
- command/event ring setup
- interrupter setup
- root-hub modeling or enumeration

## Expected Repositories

- coordination repo
- `phoenix-rtos-devices`

## Expected Files Or Subsystems

- `sources/phoenix-rtos-devices/usb/xhci/`
- `external/circle/include/circle/usb/xhci.h`
- `docs/status.md`
- `docs/source-artifacts.md`
- `tracking/current-step.md`
- `tracking/step-history.md`
- `manifests/`

## Acceptance Criteria

- `xhci` extracts and records the next pre-interrupt capability fields
- the new checks stay pre-interrupt-enable, pre-ring-allocation, and
  pre-enumeration
- the full `aarch64a72-generic-rpi4b` build remains clean with the live staged
  Pi 4 image unchanged

## Validation Plan

- fresh `aarch64a72-generic-rpi4b` build from the copied VM-local buildroot in
  `phoenix-dev`

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-22-xhci-register-layout.md`

## Notes

- Risks:
  do not widen the step into actual runtime-register use, interrupter enable,
  event-ring allocation, or root-port logic just because the related limits are
  nearby
- Dependencies:
  completed `STEP-0345` pre-interrupt xHCI capability scope
- User-visible control point before next step:
  after this step lands, `xhci` should know the remaining pre-interrupt sizing
  facts needed for later event-ring design, but should still make no
  host-operation claim
