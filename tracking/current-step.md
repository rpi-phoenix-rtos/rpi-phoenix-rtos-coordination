# Current Step

## Metadata

- Step ID: `STEP-0240`
- Title: Scope the smallest shared `lookup()`-contract review
- Status: `planned`
- Date: `2026-03-21`
- Milestone / phase: `Phase 1`

## Objective

- choose the smallest next review step that can explain the shared
  `lookup("/") -> -22` result on generic and Pi 4

## Scope

In scope:

- inspect the `lookup()` user/kernel contract across `psh`, libphoenix, and the
  kernel lookup path
- identify the narrowest likely root cause for `-EINVAL`
- document the exact next implementation step

Out of scope:

- behavior changes
- broad runtime tracing
- real hardware work
- Pi 5 or RP1 work

## Expected Repositories

- coordination repo

## Expected Files Or Subsystems

- likely `sources/libphoenix`
- likely `sources/phoenix-rtos-utils/psh/psh.c`
- likely `sources/phoenix-rtos-kernel/syscalls.c`
- likely `sources/phoenix-rtos-kernel/proc/name.c`
- `docs/status.md`
- `docs/testing-automation.md`
- `manifests/`
- `tracking/current-step.md`
- `tracking/step-history.md`

## Acceptance Criteria

- the selected next step names the exact source files to review
- the review narrows the likely root cause of the shared `-EINVAL`
- the result names one concrete implementation follow-up

## Validation Plan

- Analysis only:
  - inspect the `lookup()` wrapper and kernel-side expectations
  - verify whether `lookup("/", NULL, &oid)` is a valid call form
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-21-aarch64-psh-root-lookup-result.md`

## Notes

- Risks:
  do not jump straight into a fix before the user/kernel `lookup()` contract is
  re-read
- Dependencies:
  completed `STEP-0239` first-result visibility for `psh` `lookup("/")`
- Source reminder:
  both lanes now prove the first observed `psh` root lookup result is `-22`
- User-visible control point before next step:
  after this scope step lands, the next implementation patch should target the
  shared `lookup()` contract mismatch only
