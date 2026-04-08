# Current Step

## Metadata

- Step ID: `STEP-0421`
- Title: Scope the next Pi 4 earliest-entry diagnostic experiment from the low-level survey
- Status: `in_progress`
- Date: `2026-04-08`
- Milestone / phase: `Phase 1`

## Objective

- choose the next smallest real-hardware Pi 4 boot experiment using the new
  low-level survey, with the focus on the remaining earliest-entry mismatch
  rather than more broad source gathering

## Scope

In scope:

- choosing the next bounded code experiment from the documented Pi 4 low-level
  facts
- prioritizing:
  - fuller Pi 4 armstub register preparation
  - an even earlier visible sign-of-life path
  - a bounded earliest-entry proof such as GPIO or framebuffer activity
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
- `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/`
- `sources/plo/`
- `manifests/`

## Acceptance Criteria

- the next Pi 4 rebuild experiment is selected from the documented low-level
  evidence
- the selected experiment is smaller and better justified than the previous
  ad hoc boot guesses
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
  `manifests/2026-04-08-pi4-low-level-reference-survey.md`

## Notes

- Trigger:
  the new low-level survey confirms that the corrected Pi 4 aliases and timer
  constants are now on solid ground, while the current custom Phoenix Pi 4
  armstub is still smaller than the known-working Circle and
  `rpi4-bare-metal` stubs. The next move should therefore be a bounded
  earliest-entry experiment based on that remaining delta.
