# Current Step

## Metadata

- Step ID: `STEP-0379`
- Title: Scope the smallest xHCI context-population step before `Address Device`
- Status: `in_progress`
- Date: `2026-03-22`
- Milestone / phase: `Phase 1`

## Objective

- define the smallest context-preparation move after per-slot memory ownership,
  aimed at the minimum slot/input/EP0 fields Phoenix needs before a bounded
  `Address Device` command

## Scope

In scope:

- selecting the minimum subset of slot/input/EP0 context fields needed before
  `Address Device`
- deciding whether the first context-preparation seam should include:
  - slot context fields from `usb_dev_t`
  - EP0 max-packet and dequeue-pointer fields
  - EP0 ring layout initialization
- keeping the scope strictly below non-roothub control-transfer execution

Out of scope:

- issuing the `Address Device` command in this planning step
- broad xHCI enumeration or generic transfer support
- staging `/sbin/usb` or `/sbin/usbkbd` on the Pi 4 image
- SD-image export or manual hardware execution

## Expected Repositories

- coordination repo

## Expected Files Or Subsystems

- `tracking/current-step.md`
- `tracking/step-history.md`
- `docs/status.md`
- `docs/source-artifacts.md`
- `manifests/`

## Acceptance Criteria

- the next bounded xHCI move is explicitly selected
- the selected seam stays below command execution and below endpoint-0
  transfers
- the step explains which concrete context fields Phoenix should populate first

## Validation Plan

- source review of the current Phoenix xHCI path and Phoenix USB device model
- no code changes required for the planning step itself

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-22-xhci-slot-space.md`

## Notes

- Risks:
  avoid jumping directly into a large mixed step that combines context
  population, command execution, and endpoint-0 transfers
- Dependencies:
  completed `STEP-0378` xHCI slot-space allocation and DCBAA binding
- User-visible control point before next step:
  the next implementation step should identify the minimum concrete fields
  Phoenix needs in the input and endpoint-0 contexts before `Address Device`
