# Current Step

## Metadata

- Step ID: `STEP-0232`
- Title: Scope the smallest `psh`-local startup visibility step
- Status: `planned`
- Date: `2026-03-21`
- Milestone / phase: `Phase 1`

## Objective

- choose the smallest `psh`-local visibility patch that can divide the current
  post-spawn silence into one concrete later-boot boundary

## Scope

In scope:

- review the current `psh` startup path in `psh.c` and `pshapp.c`
- choose the minimum set of one-line markers needed to split the current stall
- document the exact next implementation step and why it is the narrowest useful
  move

Out of scope:

- changing shell behavior
- kernel or loader behavior changes
- real hardware work
- Pi 5 or RP1 work

## Expected Repositories

- coordination repo

## Expected Files Or Subsystems

- `docs/status.md`
- `manifests/`
- `tracking/current-step.md`
- `tracking/step-history.md`

## Acceptance Criteria

- the selected next patch is limited to `psh` startup visibility only
- the plan explicitly names the exact source file(s) and markers to add
- the result narrows the next move to one concrete implementation step

## Validation Plan

- Analysis only:
  - inspect `sources/phoenix-rtos-utils/psh/psh.c`
  - inspect `sources/phoenix-rtos-utils/psh/pshapp/pshapp.c`
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-21-aarch64-later-boot-interactive-probe.md`

## Notes

- Risks:
  avoid widening into speculative shell fixes before visibility narrows the
  boundary
- Dependencies:
  completed `STEP-0231` later-boot interactive-console probe
- Source reminder:
  generic already echoes input but shows no shell response, so the next split
  must happen inside the `psh` path itself
- User-visible control point before next step:
  after this scope step lands, the next implementation patch should stay in
  `psh` only and should not reopen kernel-side traces unless new evidence
  forces it
