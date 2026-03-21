# Current Step

## Metadata

- Step ID: `STEP-0261`
- Title: Scope the smallest reusable QEMU shell-smoke helper
- Status: `planned`
- Date: `2026-03-21`
- Milestone / phase: `Phase 1`

## Objective

- define the smallest reusable helper that can run the validated `help` shell
  smoke on the fast QEMU lanes without hand-written long commands

## Scope

In scope:

- choose the smallest helper form for the now-validated QEMU `help` smoke
- keep the helper limited to the existing fast lanes:
  - `aarch64a53-generic-qemu`
  - `aarch64a72-generic-rpi4b`
- keep the command under test fixed to `help`

Out of scope:

- changing Phoenix source code
- broad QEMU test-harness design
- boot-media work
- real hardware work

## Expected Repositories

- coordination repo

## Expected Files Or Subsystems

- QEMU runtime validation flow in `phoenix-dev`
- existing generic and Pi 4 smoke logs in `/tmp`
- `scripts/`
- `docs/status.md`
- `manifests/`
- `tracking/current-step.md`
- `tracking/step-history.md`

## Acceptance Criteria

- one concrete helper shape is selected
- the helper scope stays small enough to implement in the next step
- the helper design reuses the already validated QEMU arguments and smoke
  markers

## Validation Plan

- Source review:
  examine the validated smoke commands and choose the minimum wrapper
- Runtime planning:
  keep the next step limited to packaging, then rerun both lanes
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-21-aarch64-generic-shell-stdin-validation.md`

## Notes

- Risks:
  avoid turning a small smoke helper into a full lab framework
- Dependencies:
  completed `STEP-0260` generic stdin-path validation
- Source reminder:
  both fast lanes now pass the same `help` smoke
- User-visible control point before next step:
  after this scope lands, the next step should implement only that helper and
  rerun the same validated smoke
