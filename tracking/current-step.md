# Current Step

## Metadata

- Step ID: `STEP-0396`
- Title: Scope the smallest structured operator-facing Pi 4 first-trial checklist step
- Status: `in_progress`
- Date: `2026-03-22`
- Milestone / phase: `Phase 1`

## Objective

- define the smallest operator-facing artifact that will make the first Pi 4
  HDMI plus USB-keyboard trial produce actionable evidence

## Scope

In scope:

- scoping a dedicated first-trial checklist or report template
- keeping the step operator-facing and evidence-focused
- improving the handoff without adding speculative code work

Out of scope:

- manual hardware execution itself
- new code-side USB or xHCI feature work until board evidence exists
- wider packaging such as `phoenix-rtos-ports`

## Expected Repositories

- coordination repo

## Expected Files Or Subsystems

- `docs/manual-operator-instructions.md`
- `docs/`
- `docs/status.md`
- `tracking/current-step.md`
- `tracking/step-history.md`
- `docs/source-artifacts.md`
- `manifests/`

## Acceptance Criteria

- the next operator-facing artifact is explicitly identified
- the artifact is justified as improving evidence quality rather than changing
  runtime behavior
- no speculative code-side blocker is invented

## Validation Plan

- review the current runbook and identify the smallest missing operator-facing
  structure for the first board trial

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-22-rpi4b-first-manual-trial-scope.md`

## Notes

- Risks:
  avoid widening the next move into runtime source changes before the first
  hardware result
- Dependencies:
  completed `STEP-0395` first manual-trial scope
- User-visible control point before next step:
  after this step, the next bounded move should be either a checklist/template
  implementation or the actual board boot
