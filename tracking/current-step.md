# Current Step

## Metadata

- Step ID: `STEP-0364`
- Title: Implement the smallest xHCI runtime-register programming step for the new event ring
- Status: `in_progress`
- Date: `2026-03-22`
- Milestone / phase: `Phase 1`

## Objective

- bind the already allocated event-ring structures into the xHCI runtime
  register block for interrupter `0`

## Scope

In scope:

- programming `ERSTSZ`, `ERSTBA`, and `ERDP` for interrupter `0`
- reading the same registers back and validating the programmed values
- keeping the step pre-interrupt-enable, pre-doorbell, and pre-enumeration

Out of scope:

- setting interrupter enable bits
- doorbell use, command submission, root-hub logic, or enumeration
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

- the xHCI path records the programmed runtime-register state for interrupter
  `0`:
  - one-entry `ERSTSZ`
  - `ERSTBA`
  - `ERDP`
- the step stays pre-interrupt-enable, pre-doorbell, and pre-enumeration
- the full `aarch64a72-generic-rpi4b` build still succeeds

## Validation Plan

- fresh `aarch64a72-generic-rpi4b` build from the copied VM-local buildroot in
  `phoenix-dev`
- no live-image or QEMU behavior change is required beyond preserved build
  success because the xHCI path still returns `-ENOSYS`

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-22-xhci-event-ring-programming-scope.md`

## Notes

- Risks:
  avoid enabling runtime event delivery before the runtime-register state is
  cleanly programmed and read back
- Dependencies:
  completed `STEP-0363` xHCI event-ring programming scope
- User-visible control point before next step:
  the next implementation step should still keep the staged Pi 4 image
  behavior unchanged while strengthening controller event-delivery preparation
