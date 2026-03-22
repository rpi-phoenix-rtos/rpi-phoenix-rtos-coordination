# Current Step

## Metadata

- Step ID: `STEP-0360`
- Title: Implement the smallest xHCI run-state self-test beyond command-space binding
- Status: `in_progress`
- Date: `2026-03-22`
- Milestone / phase: `Phase 1`

## Objective

- add the first bounded operational-state validation after `DCBAAP`, `CRCR`,
  and `CONFIG` programming without yet claiming a usable host controller

## Scope

In scope:

- adding the first `RUN/STOP` self-test for the xHCI controller
- verifying halted-state transition into run and back into halt
- rejecting immediate host/system error states during that self-test
- keeping `xhci_init()` non-production by still returning `-ENOSYS`

Out of scope:

- event-ring or interrupter allocation/programming
- doorbell use, command submission, or root-hub logic
- USB-device enumeration
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

- the xHCI path can:
  - observe halted state after reset
  - clear halted state by setting `RUN/STOP`
  - return to halted state after clearing `RUN/STOP`
- the step stays pre-event-ring, pre-interrupt-enable, pre-doorbell, and
  pre-enumeration
- the full `aarch64a72-generic-rpi4b` build still succeeds

## Validation Plan

- fresh `aarch64a72-generic-rpi4b` build from the copied VM-local buildroot in
  `phoenix-dev`
- no live-image or QEMU behavior change is required beyond preserved build
  success because the xHCI path still returns `-ENOSYS`

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-22-xhci-run-state-scope.md`

## Notes

- Risks:
  avoid silently relying on unsupported controller state if `HCHalted` or host
  error bits do not behave as expected
- Dependencies:
  completed `STEP-0359` xHCI run-state scope
- User-visible control point before next step:
  the next implementation step should still leave the staged Pi 4 image
  behavior unchanged while strengthening controller-state validation
