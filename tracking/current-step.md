# Current Step

## Metadata

- Step ID: `STEP-0366`
- Title: Implement the smallest xHCI command-ring layout initialization step
- Status: `in_progress`
- Date: `2026-03-22`
- Milestone / phase: `Phase 1`

## Objective

- turn the allocated command-ring backing memory into a minimal valid xHCI ring
  layout without yet submitting commands

## Scope

In scope:

- adding the final command-ring link TRB
- establishing the initial command-ring cycle-state contract in memory
- validating the link TRB target and ring geometry
- keeping the step pre-doorbell, pre-interrupt-enable, and pre-enumeration

Out of scope:

- ringing the command doorbell
- enabling interrupts
- root-hub logic or enumeration
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

- the xHCI path now records a minimal valid command-ring memory layout with:
  - a final link TRB
  - a stable initial cycle-state contract
- the step stays pre-doorbell, pre-interrupt-enable, and pre-enumeration
- the full `aarch64a72-generic-rpi4b` build still succeeds

## Validation Plan

- fresh `aarch64a72-generic-rpi4b` build from the copied VM-local buildroot in
  `phoenix-dev`
- no live-image or QEMU behavior change is required beyond preserved build
  success because the xHCI path still returns `-ENOSYS`

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-22-xhci-command-ring-layout-scope.md`

## Notes

- Risks:
  avoid mixing command submission policy into what should still be a memory-only
  ring-layout step
- Dependencies:
  completed `STEP-0365` xHCI command-ring layout scope
- User-visible control point before next step:
  the next implementation step should still keep the staged Pi 4 image
  behavior unchanged while making the future command path less speculative
