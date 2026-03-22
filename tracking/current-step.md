# Current Step

## Metadata

- Step ID: `STEP-0404`
- Title: Implement the Pi 4 first-trial report helper
- Status: `in_progress`
- Date: `2026-03-22`
- Milestone / phase: `Phase 1`

## Objective

- add one final non-destructive helper that generates a prefilled first-trial
  report file from the current artifact state

## Scope

In scope:

- one small report-template generation helper
- keeping the helper tied to the current Pi 4 artifact
- documenting the helper in the existing runbook

Out of scope:

- manual hardware execution itself
- new code-side USB or xHCI feature work until board evidence exists
- automatic flashing or other destructive helpers

## Expected Repositories

- coordination repo

## Expected Files Or Subsystems

- `docs/manual-operator-instructions.md`
- `docs/pi4-first-hardware-trial.md`
- `scripts/`
- `docs/status.md`
- `tracking/current-step.md`
- `tracking/step-history.md`
- `docs/source-artifacts.md`
- `manifests/`

## Acceptance Criteria

- one report helper exists and produces a prefilled report file
- the runbook points at that helper
- no runtime behavior is changed

## Validation Plan

- run the helper and inspect the generated output file
- confirm the file contains the current image path and checksum

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-22-rpi4b-report-helper-scope.md`

## Notes

- Risks:
  avoid widening this into a larger reporting system
- Dependencies:
  completed `STEP-0403` report-helper scope
- User-visible control point before next step:
  after this step, the next bounded move should be the user's first board boot
  result or report file
