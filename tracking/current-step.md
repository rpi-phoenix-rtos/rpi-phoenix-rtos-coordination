# Current Step

## Metadata

- Step ID: `STEP-0423`
- Title: Choose the next Pi 4 earliest-entry diagnostic experiment from the expanded survey
- Status: `in_progress`
- Date: `2026-04-08`
- Milestone / phase: `Phase 1`

## Objective

- choose the next smallest real-hardware Pi 4 boot experiment using the
  expanded low-level survey, with the focus on the remaining earliest-entry
  mismatch rather than more broad source gathering

## Scope

In scope:

- choosing the next bounded code experiment from the documented Pi 4 low-level
  facts
- prioritizing:
  - GPIO42 activity-LED earliest-entry proof
  - fuller Pi 4 armstub register preparation
  - a bounded controller-selection proof if the GIC-versus-legacy path still
    looks ambiguous
  - an even earlier visible sign-of-life path before the current HDMI lane
- keeping the choice narrow enough for one rebuild and one board retry

Out of scope:

- broader documentation harvesting
- unrelated cleanup or later-stage subsystem work

## Expected Repositories

- coordination repo
- `phoenix-rtos-project`
- possibly `plo`

## Expected Files Or Subsystems

- `docs/raspberry-pi-4-low-level-reference-survey.md`
- `docs/status.md`
- `tracking/current-step.md`
- `tracking/step-history.md`
- `docs/source-artifacts.md`
- `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/`
- `sources/plo/`
- `manifests/`

## Acceptance Criteria

- the next Pi 4 rebuild experiment is selected from the expanded low-level
  evidence
- the selected experiment is smaller and better justified than the previous ad
  hoc boot guesses
- the chosen path is recorded in docs and tracking before code changes begin

## Validation Plan

- use `docs/raspberry-pi-4-low-level-reference-survey.md` plus the existing
  Pi 4 hardware evidence to justify the next specific experiment
- record the selection in:
  - `docs/status.md`
  - `tracking/current-step.md`
  - a new manifest

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-04-08-pi4-low-level-reference-survey-followup.md`

## Notes

- Trigger:
  the expanded survey now makes the most plausible next Pi 4 earliest-entry
  paths more explicit: GPIO42 activity-LED proof, fuller armstub register
  setup, or a bounded controller-selection proof if the firmware is still not
  leaving the board in the expected GIC mode.
