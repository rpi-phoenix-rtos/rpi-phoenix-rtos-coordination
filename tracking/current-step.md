# Current Step

## Metadata

- Step ID: `STEP-0237`
- Title: Implement bounded `psh` root-lookup success visibility
- Status: `planned`
- Date: `2026-03-21`
- Milestone / phase: `Phase 1`

## Objective

- prove whether `psh` gets past its `lookup("/")` wait loop using one bounded
  kernel-side marker

## Scope

In scope:

- review the current `psh` startup path in `psh.c` and `pshapp.c`
- add the selected one-time `psh` root-lookup success marker
- rebuild the generic and Pi 4 QEMU lanes
- record whether either lane resolves `/` successfully

Out of scope:

- changing behavior
- broad syscall tracing
- real hardware work
- Pi 5 or RP1 work

## Expected Repositories

- `phoenix-rtos-kernel`
- coordination repo

## Expected Files Or Subsystems

- `sources/phoenix-rtos-kernel/syscalls.c`
- `docs/status.md`
- `manifests/`
- `tracking/current-step.md`
- `tracking/step-history.md`

## Acceptance Criteria

- generic QEMU proves whether `psh` resolves `/`
- Pi 4 QEMU is rerun too if the result remains on the shared path
- the result narrows the next move to one concrete follow-up

## Validation Plan

- Emulator:
  - rebuild generic `virt`
  - rerun generic QEMU
  - rerun Pi 4 QEMU if the generic result remains shared
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-21-aarch64-psh-root-lookup-scope.md`

## Notes

- Risks:
  keep the trace one-time and `psh`-filtered so it does not flood the syscall
  path
- Dependencies:
  completed `STEP-0236` `psh` root-lookup success trace scope
- Source reminder:
  both lanes now prove `psh` reaches user mode, so the next split should move
  to the earliest `psh`-specific syscall result
- User-visible control point before next step:
  after this step lands, the next follow-up should depend on whether `psh`
  resolves `/` at all
