# Current Step

## Metadata

- Step ID: `STEP-0381`
- Title: Scope the smallest xHCI address-contract step before `Address Device`
- Status: `in_progress`
- Date: `2026-03-22`
- Milestone / phase: `Phase 1`

## Objective

- define the smallest correct bridge between Phoenix USB enumeration and xHCI
  slot-based addressing before implementing the first bounded
  `Address Device` command path

## Scope

In scope:

- deciding how Phoenix `REQ_SET_ADDRESS` should map onto xHCI `Address Device`
- deciding whether the next seam should:
  - honor the Phoenix-requested USB address directly
  - couple the USB address to the xHCI slot ID
  - add a bounded translation contract in the HCD path
- keeping the scope below generic endpoint-0 transfer execution

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

- the next bounded `Address Device` move is explicitly selected
- the selected seam explains the USB-address versus slot-ID contract
- the scope stays below generic endpoint-0 transfer execution and below broad
  enumeration

## Validation Plan

- source review of the current Phoenix USB enumeration path and the current
  xHCI child-device state
- no code changes required for the planning step itself

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-22-xhci-context-population.md`

## Notes

- Risks:
  avoid implementing `Address Device` on a wrong address contract that would
  fight Phoenix's current HCD address allocator
- Dependencies:
  completed `STEP-0380` xHCI context preparation for the first direct-root-port
  child path
- User-visible control point before next step:
  the next implementation step should say whether the first bounded
  `Address Device` wrapper uses slot ID as the effective USB address or
  introduces an explicit translation rule
