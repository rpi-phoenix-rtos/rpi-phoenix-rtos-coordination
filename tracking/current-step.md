# Current Step

## Metadata

- Step ID: `STEP-0335`
- Title: Scope the smallest first xHCI controller-reset slice
- Status: `in_progress`
- Date: `2026-03-21`
- Milestone / phase: `Phase 1`

## Objective

- define the smallest controller-reset and operational-readiness slice that
  should follow the new runtime-safe xHCI capability probe

## Scope

In scope:

- reviewing the remaining gap between the new xHCI capability probe and the
  first controller-side readiness step
- selecting the smallest reset-oriented xHCI move that can follow the current
  capability probe without widening into enumeration or staged runtime support
- keeping the current HDMI text-console baseline and deferred SD export intact

Out of scope:

- SD-image export or checksum refresh
- manual hardware execution
- staged runtime USB host support
- command/event ring setup
- interrupt setup
- root-hub modeling or enumeration
- changes to the existing `usbkbd` or `pl011-tty` logic
- broad BCM2711 PCIe host-bridge redesign

## Expected Repositories

- coordination repo
- `phoenix-rtos-devices`

## Expected Files Or Subsystems

- `sources/phoenix-rtos-devices/usb/xhci/`
- `docs/status.md`
- `docs/source-artifacts.md`
- `tracking/current-step.md`
- `tracking/step-history.md`
- `manifests/`

## Acceptance Criteria

- the next xHCI step is explicitly bounded to the smallest reset/readiness
  slice justified by the current Phoenix USB HCD contract and the new probe
  state
- the next step still avoids staged runtime USB support and enumeration
- the tracking docs clearly state why reset comes before firmware-notify,
  interrupts, rings, or root-hub work

## Validation Plan

- source inspection against:
  - `sources/phoenix-rtos-devices/usb/xhci/xhci.c`
  - `sources/phoenix-rtos-usb/usb/hcd.c`
  - `external/circle/include/circle/usb/xhci.h`
  - `external/circle/lib/usb/xhcidevice.cpp`

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-21-xhci-runtime-safe-init.md`

## Notes

- Risks:
  do not make `xhci_init()` return success before there is enough controller
  state for the Phoenix USB stack to enumerate safely
- Dependencies:
  completed `STEP-0334` first runtime-safe xHCI init slice
- User-visible control point before next step:
  after this step lands, the repo should show the exact smallest xHCI reset
  move and why it comes before broader runtime USB bring-up
