# Current Step

## Metadata

- Step ID: `STEP-0238`
- Title: Scope the smallest first-attempt `psh` root-lookup trace
- Status: `planned`
- Date: `2026-03-21`
- Milestone / phase: `Phase 1`

## Objective

- choose the smallest next hook that can distinguish “`psh` failed `lookup("/")`
  and is looping” from “`psh` never reached that syscall at all”

## Scope

In scope:

- review the current `psh` startup path in `psh.c` and `pshapp.c`
- inspect the current `syscalls_lookup()` trace point
- choose the minimum change needed to expose the first `psh` lookup attempt and
  its result for `/`
- document the exact next implementation step

Out of scope:

- changing behavior
- broad syscall tracing
- real hardware work
- Pi 5 or RP1 work

## Expected Repositories

- coordination repo

## Expected Files Or Subsystems

- likely `sources/phoenix-rtos-kernel/syscalls.c`
- `docs/status.md`
- `manifests/`
- `tracking/current-step.md`
- `tracking/step-history.md`

## Acceptance Criteria

- the selected next patch exposes one first-attempt `psh` root lookup result
- the scope names the source file and exact trigger condition
- the result narrows the next move to one concrete implementation step

## Validation Plan

- Analysis only:
  - inspect the current `psh`-filtered `syscalls_lookup()` trace
  - choose the smallest first-attempt trace that captures success or failure
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-21-aarch64-psh-root-lookup.md`

## Notes

- Risks:
  keep the next trace one-time and `/`-specific so it does not reopen general
  pathname logging
- Dependencies:
  completed `STEP-0237` `psh` root-lookup success visibility
- Source reminder:
  both lanes prove `psh` reaches user mode but still do not prove a successful
  root lookup
- User-visible control point before next step:
  after this scope step lands, the next patch should expose the first `psh`
  root-lookup result only
