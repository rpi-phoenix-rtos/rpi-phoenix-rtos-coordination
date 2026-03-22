# Current Step

## Metadata

- Step ID: `STEP-0362`
- Title: Implement the smallest xHCI event-ring allocation step
- Status: `in_progress`
- Date: `2026-03-22`
- Milestone / phase: `Phase 1`

## Objective

- add the first event-ring memory foundation needed before runtime interrupter
  register programming

## Scope

In scope:

- allocating one event-ring segment
- allocating one ERST block for interrupter 0
- populating one ERST entry that points at the allocated event-ring segment
- recording the corresponding physical addresses and event-ring TRB count

Out of scope:

- runtime-register programming for `ERSTSZ`, `ERSTBA`, or `ERDP`
- interrupt-enable, doorbell, root-hub, or enumeration logic
- SD-image export or checksum refresh
- manual hardware execution
- unrelated shell, console, or PCIe changes

## Expected Repositories

- coordination repo
- `phoenix-rtos-kernel`

## Expected Files Or Subsystems

- `sources/phoenix-rtos-devices/usb/xhci/xhci.c`
- `docs/status.md`
- `docs/source-artifacts.md`
- `tracking/current-step.md`
- `tracking/step-history.md`
- `manifests/`

## Acceptance Criteria

- the xHCI path records:
  - one aligned event-ring segment
  - one aligned ERST block
  - one populated ERST entry for interrupter 0
- the step stays pre-runtime-register-programming, pre-interrupt-enable,
  pre-doorbell, and pre-enumeration
- the full `aarch64a72-generic-rpi4b` build still succeeds

## Validation Plan

- fresh `aarch64a72-generic-rpi4b` build from the copied VM-local buildroot in
  `phoenix-dev`
- no live-image or QEMU behavior change is required beyond preserved build
  success because the xHCI path still returns `-ENOSYS`

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-22-xhci-event-ring-allocation-scope.md`

## Notes

- Risks:
  avoid silently introducing event-ring layout assumptions that are broader than
  one-segment, one-interrupter preparation
- Dependencies:
  completed `STEP-0361` xHCI event-ring allocation scope
- User-visible control point before next step:
  the next implementation step should still leave the staged Pi 4 image
  behavior unchanged while preparing the first event-delivery structures
