# Current Step

## Metadata

- Step ID: `STEP-0284`
- Title: Scope the smallest automated regression check for the Pi 4 HDMI visibility path
- Status: `planned`
- Date: `2026-03-21`
- Milestone / phase: `Phase 1`

## Objective

- define the smallest next step that turns the new Pi 4 `plo` HDMI visibility
  path into a repeatable regression check rather than a one-off manual QEMU
  experiment

## Scope

In scope:

- define one automated-friendly validation shape for the current early HDMI path
- keep the step limited to the existing `plo` marker visibility behavior
- prefer a coordination-repo helper or documented smoke path over wider source
  work

Out of scope:

- runtime framebuffer console support
- new framebuffer rendering logic
- real hardware execution
- broad graphics, USB, or network bring-up

## Expected Repositories

- coordination repo

## Expected Files Or Subsystems

- `docs/testing-automation.md`
- `docs/status.md`
- `scripts/`
- `manifests/`
- `tracking/current-step.md`
- `tracking/step-history.md`

## Acceptance Criteria

- the next implementation move is reduced to one explicit regression-check
  shape for the current Pi 4 HDMI marker
- the step does not widen into new framebuffer or firmware logic
- the result records what exact artifact or pixel signature the automated check
  should validate

## Validation Plan

- Build:
  not applicable
- Emulator:
  not applicable
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-21-aarch64-rpi4b-plo-hdmi-visibility.md`

## Notes

- Risks:
  avoid widening into a runtime display subsystem or a broad test harness
- Dependencies:
  completed `STEP-0283` Pi 4 `plo` HDMI visibility
- User-visible control point before next step:
  after this step lands, the next bounded move should either implement the
  agreed regression helper or pivot to the smallest real-hardware HDMI
  readiness refinement
