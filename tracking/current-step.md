# Current Step

## Metadata

- Step ID: `STEP-0375`
- Title: Scope the smallest xHCI non-roothub device-enumeration step
- Status: `in_progress`
- Date: `2026-03-22`
- Milestone / phase: `Phase 1`

## Objective

- define the first bounded xHCI step that moves beyond the roothub and toward
  real child-device enumeration, which is now the next hard blocker for USB
  keyboard support

## Scope

In scope:

- deciding what the smallest first non-roothub xHCI seam should be
- keeping the scope at the first child-device enumeration boundary

Out of scope:

- broad xHCI device-enumeration implementation in this planning step
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

- the next bounded xHCI device-enumeration move is explicitly selected
- the scope stays narrow and pre-keyboard claims
- the next implementation step explains whether the first seam should be slot
  enable, address-device, or another smaller prerequisite

## Validation Plan

- source-level review of the current Phoenix USB enumeration path and the
  current xHCI command/ring state
- no code changes required for the planning step itself

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-22-xhci-roothub-status.md`

## Notes

- Risks:
  avoid jumping directly into an oversized “make keyboard work” patch without a
  clear first xHCI child-device seam
- Dependencies:
  completed `STEP-0374` xHCI roothub status-delivery implementation
- User-visible control point before next step:
  the next implementation step should identify the first concrete xHCI
  child-device operation Phoenix needs after the roothub
