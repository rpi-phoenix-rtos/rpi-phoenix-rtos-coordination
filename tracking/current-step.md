# Current Step

## Metadata

- Step ID: `STEP-0377`
- Title: Scope the smallest xHCI `Address Device` step
- Status: `in_progress`
- Date: `2026-03-22`
- Milestone / phase: `Phase 1`

## Objective

- define the first bounded xHCI step after `Enable Slot`, aimed at the minimum
  controller work needed to move toward real endpoint-0 control transfers

## Scope

In scope:

- deciding the smallest correct `Address Device` seam
- keeping the scope at the first endpoint-0 / input-context boundary after
  `Enable Slot`

Out of scope:

- broad xHCI enumeration implementation in this planning step
- staging `/sbin/usb` or `/sbin/usbkbd` on the Pi 4 image
- SD-image export or checksum refresh
- manual hardware execution
- unrelated shell, HDMI, or PCIe changes

## Expected Repositories

- coordination repo

## Expected Files Or Subsystems

- `tracking/current-step.md`
- `tracking/step-history.md`
- `docs/status.md`
- `docs/source-artifacts.md`
- `manifests/`

## Acceptance Criteria

- the next bounded xHCI `Address Device` move is explicitly selected
- the scope stays narrow and pre-keyboard claims
- the next implementation step explains whether the first seam should be input
  context allocation, slot-context population, endpoint-0 ring setup, or a
  smaller prerequisite

## Validation Plan

- source-level review of the current Phoenix USB enumeration path and the
  current xHCI command/ring/controller state
- no code changes required for the planning step itself

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-22-xhci-enable-slot.md`

## Notes

- Risks:
  avoid jumping directly into a large endpoint-0 implementation without a clear
  first `Address Device` seam
- Dependencies:
  completed `STEP-0376` xHCI `Enable Slot` implementation
- User-visible control point before next step:
  the next implementation step should identify the first concrete xHCI
  controller object Phoenix needs after slot allocation
