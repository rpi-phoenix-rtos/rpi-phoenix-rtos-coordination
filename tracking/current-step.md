# Current Step

## Metadata

- Step ID: `STEP-0227`
- Title: Scope one bounded later-boot Pi 4 parity check
- Status: `planned`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- choose the smallest next comparison that identifies the first Pi 4 blocker
  after the now-working early GIC / timer / console path

## Scope

In scope:

- review the new generic-versus-Pi4 runtime parity through
  `dummyfs: initialized`
- choose one bounded later-boot comparison or visibility step
- keep the next move outside the already-fixed early GIC / timer path
- prefer a read-only or minimally invasive step if possible

Out of scope:

- new kernel or loader behavior changes
- real hardware work
- broad test-framework redesign
- Pi 5 or RP1 work

## Expected Repositories

- coordination repo

## Expected Files Or Subsystems

- `docs/status.md`
- `docs/testing-automation.md`
- manifests and tracking updates for this scope step

## Acceptance Criteria

- the next later-boot step is narrowed to one concrete question
- the selected question is explicitly outside the solved early GIC path
- the step remains aligned with getting the first Pi 4 boot as quickly as
  possible

## Validation Plan

- Review:
  inspect the current generic and Pi 4 runtime evidence after the DTB fix
- Evidence:
  - use `manifests/2026-03-20-aarch64-pi4-gic-dtb-fix.md`
  - compare the latest generic and Pi 4 QEMU traces after `dummyfs: initialized`
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-pi4-gic-dtb-fix.md`

## Notes

- Risks:
  do not re-open the solved early GIC path unless new evidence forces it
- Dependencies:
  completed `STEP-0226` Pi 4 GIC DTB discovery fix
- Source reminder:
  Pi 4 now reaches `pl011-tty: console ready` and `dummyfs: initialized` under
  `raspi4b` QEMU
- User-visible control point before next step:
  after this step lands, the next implementation move should be chosen by the
  first observed later-boot mismatch between generic and Pi 4, not by old timer
  hypotheses
