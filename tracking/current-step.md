# Current Step

## Metadata

- Step ID: `STEP-0395`
- Title: Scope the first manual Raspberry Pi 4 HDMI plus USB-keyboard execution step
- Status: `in_progress`
- Date: `2026-03-22`
- Milestone / phase: `Phase 1`

## Objective

- define the first bounded real-device execution step after exporting the Pi 4
  HDMI plus USB-keyboard SD-card image

## Scope

In scope:

- defining the next manual validation step on real Raspberry Pi 4 hardware
- keeping the next move explicitly operator-facing
- confirming that the current software-side work is handed off as a board-trial
  image rather than widened into speculative new code work

Out of scope:

- manual hardware execution itself
- new code-side USB or xHCI feature work until board evidence exists
- wider packaging such as `phoenix-rtos-ports`

## Expected Repositories

- coordination repo

## Expected Files Or Subsystems

- `docs/manual-operator-instructions.md`
- `docs/status.md`
- `tracking/current-step.md`
- `tracking/step-history.md`
- `docs/source-artifacts.md`
- `manifests/`

## Acceptance Criteria

- the next move is explicitly identified as manual board execution
- the docs point at the exported SD image and its checksum
- no additional code-side blocker is claimed without new board evidence

## Validation Plan

- review the current exported-artifact state and operator runbook
- confirm that the next stronger lane requires manual hardware execution

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-22-rpi4b-sdimg-refresh.md`

## Notes

- Risks:
  avoid widening the next move into speculative source changes before the first
  hardware result
- Dependencies:
  completed `STEP-0394` SD-image refresh implementation step
- User-visible control point before next step:
  after this step, the next bounded move should be the user's first board boot
  result or a newly discovered operator-side blocker
