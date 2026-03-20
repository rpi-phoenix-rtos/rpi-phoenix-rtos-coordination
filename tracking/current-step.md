# Current Step

## Metadata

- Step ID: `STEP-0235`
- Title: Implement bounded `psh` first-user-schedule visibility
- Status: `planned`
- Date: `2026-03-21`
- Milestone / phase: `Phase 1`

## Objective

- prove whether the spawned `psh` process ever reaches first user execution
  using one bounded kernel-side marker

## Scope

In scope:

- review the current `psh` startup path in `psh.c` and `pshapp.c`
- add the selected one-time `psh` user-schedule marker
- rebuild the generic and Pi 4 QEMU lanes
- record whether either lane reaches the first-user-execution boundary

Out of scope:

- changing behavior
- broad scheduler tracing
- real hardware work
- Pi 5 or RP1 work

## Expected Repositories

- `phoenix-rtos-kernel`
- coordination repo

## Expected Files Or Subsystems

- `sources/phoenix-rtos-kernel/proc/threads.c`
- `docs/status.md`
- `manifests/`
- `tracking/current-step.md`
- `tracking/step-history.md`

## Acceptance Criteria

- generic QEMU proves whether `psh` reaches first user execution
- Pi 4 QEMU is rerun too if the result stays on the shared path
- the result narrows the next move to one concrete follow-up

## Validation Plan

- Emulator:
  - rebuild generic `virt`
  - rerun generic QEMU
  - rerun Pi 4 QEMU if the generic result remains in the shared path
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-21-aarch64-psh-first-user-schedule-scope.md`

## Notes

- Risks:
  keep the trace one-time and `psh`-specific so the scheduler output stays
  readable
- Dependencies:
  completed `STEP-0234` below-stdio `psh` process-entry visibility scope
- Source reminder:
  neither lane shows any `psh:` marker at all, so the next split has to happen
  below shell-visible stdio
- User-visible control point before next step:
  after this step lands, the next follow-up should depend on whether `psh`
  reaches first user execution at all
