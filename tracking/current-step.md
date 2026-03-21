# Current Step

## Metadata

- Step ID: `STEP-0296`
- Title: Scope the smallest post-Circle Pi 4 HDMI refinement
- Status: `planned`
- Date: `2026-03-21`
- Milestone / phase: `Phase 1`

## Objective

- select the smallest next technical step after the Circle review, with bias
  toward HDMI-visible progress for the first manual Pi 4 trial

## Scope

In scope:

- use the new Circle review to choose the next bounded Pi 4 step
- prefer a tiny HDMI-visible refinement over broader subsystem work
- keep the step selection compatible with the current no-UART real-board lab

Out of scope:

- PCIe bring-up
- xHCI bring-up
- USB keyboard implementation
- broad runtime graphics support

## Expected Repositories

- coordination repo
- possibly `plo`
- possibly `phoenix-rtos-project`

## Expected Files Or Subsystems

- `docs/circle-reference-review.md`
- `docs/platforms/raspberry-pi-4.md`
- `docs/status.md`
- `manifests/`
- `tracking/current-step.md`
- `tracking/step-history.md`

## Acceptance Criteria

- exactly one small next step is selected
- the selected step is explicitly justified against the Circle findings
- the result stays narrow and practical for the current lab setup

## Validation Plan

- Build:
  not applicable
- Emulator:
  not applicable
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-21-circle-pi4-video-usb-review.md`

## Notes

- Risks:
  avoid widening into PCIe or xHCI work just because Circle contains it
- Dependencies:
  completed `STEP-0295` Circle review
- User-visible control point before next step:
  after this step lands, the next bounded move should either implement one
  small HDMI-oriented refinement or declare readiness for the first manual Pi 4
  trial
