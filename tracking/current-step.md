# Current Step

## Metadata

- Step ID: `STEP-0278`
- Title: Scope the smallest host-visible Pi 4 SD-card image handoff step
- Status: `planned`
- Date: `2026-03-21`
- Milestone / phase: `Phase 1`

## Objective

- select the smallest next step that makes the new Pi 4 full disk image
  directly usable from the macOS host

## Scope

In scope:

- decide whether the next bounded move should export the VM-local Pi 4 full disk
  image, fold export into the builder, or jump straight to flashing guidance
- keep the step artifact-only and no-hardware
- keep the decision aligned with the current host or operator path

Out of scope:

- implementing the host-visible export helper itself
- changing Phoenix source code
- real hardware execution

## Expected Repositories

- coordination repo

## Expected Files Or Subsystems

- VM-local `rpi4b-sd.img`
- `scripts/assemble-rpi4b-sdimg.sh`
- current host-visible artifact directory
- current manual hardware instructions
- `docs/status.md`
- `manifests/`
- `tracking/current-step.md`
- `tracking/step-history.md`

## Acceptance Criteria

- the next artifact handoff step is selected explicitly
- the selection preserves the current small-step artifact flow rather than
  widening into flashing automation or real hardware execution
- no Phoenix upstream repo changes are introduced

## Validation Plan

- inspect the current VM-local `rpi4b-sd.img` output and operator workflow gap
- confirm whether host visibility is the next smallest missing link
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-21-aarch64-rpi4b-sdimg-helper.md`

## Notes

- Risks:
  avoid widening into SD-writing automation or first-hardware smoke execution
- Dependencies:
  completed `STEP-0277` full SD-card image helper
- User-visible control point before next step:
  after the scope decision, the next bounded code step can add one host-visible
  export helper for the full SD-card image
