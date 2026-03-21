# Current Step

## Metadata

- Step ID: `STEP-0246`
- Title: Scope the smallest `posix_open()` or `proc_lookup()` console split
- Status: `planned`
- Date: `2026-03-21`
- Milestone / phase: `Phase 1`

## Objective

- choose the smallest next visibility step on the real failing
  `/dev/console` open path

## Scope

In scope:

- inspect the shared `open("/dev/console")` path in the kernel
- identify whether `posix_open()` or `proc_lookup()` is the tighter next seam

Out of scope:

- implementation changes outside minimal read-only inspection
- shell-policy changes
- console-device selection changes
- unrelated kernel or project changes
- Pi 5 or RP1 work
- real hardware work

## Expected Repositories

- coordination repo

## Expected Files Or Subsystems

- `sources/phoenix-rtos-kernel/posix/posix.c`
- `sources/phoenix-rtos-kernel/proc/name.c`
- `docs/status.md`
- `docs/source-artifacts.md`
- `manifests/`
- `tracking/current-step.md`
- `tracking/step-history.md`

## Acceptance Criteria

- the next shared blocker is bounded to one concrete `open("/dev/console")`
  follow-up
- the selected follow-up does not widen the work beyond the shared fast lane
- the result is captured in one manifest and the next active step

## Validation Plan

- Analysis only:
  not applicable
- Emulator:
  not required unless source review leaves ambiguity that needs one minimal
  runtime split
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-21-aarch64-console-lookup-via-syscalls.md`

## Notes

- Risks:
  avoid jumping to a broad console fix before the failing open path is traced in
  the correct layer
- Dependencies:
  completed `STEP-0245` negative `syscalls_lookup()` experiment
- Source reminder:
  `open()` reaches `posix_open()`, and `posix_open()` uses `proc_lookup()`
  directly
- User-visible control point before next step:
  after this step lands, the next implementation step should target one
  concrete `posix_open()` or `proc_lookup()` seam
