# Current Step

## Metadata

- Step ID: `STEP-0233`
- Title: Implement bounded `psh` startup visibility
- Status: `planned`
- Date: `2026-03-21`
- Milestone / phase: `Phase 1`

## Objective

- divide the current post-spawn silence into one concrete `psh` startup
  boundary using the smallest process-local marker patch

## Scope

In scope:

- review the current `psh` startup path in `psh.c` and `pshapp.c`
- add the selected small marker set in `psh.c` and `pshapp.c`
- rebuild the generic and Pi 4 QEMU lanes
- record which startup boundary is now visible

Out of scope:

- changing shell behavior
- kernel or loader behavior changes
- real hardware work
- Pi 5 or RP1 work

## Expected Repositories

- `phoenix-rtos-utils`
- coordination repo

## Expected Files Or Subsystems

- `sources/phoenix-rtos-utils/psh/psh.c`
- `sources/phoenix-rtos-utils/psh/pshapp/pshapp.c`
- `docs/status.md`
- `manifests/`
- `tracking/current-step.md`
- `tracking/step-history.md`

## Acceptance Criteria

- generic QEMU shows the highest visible `psh` startup marker reached
- Pi 4 QEMU is rerun too if the generic result stays in the shared path
- the result narrows the next move to one concrete follow-up

## Validation Plan

- Emulator:
  - rebuild generic `virt`
  - rerun the existing generic QEMU lane
  - rerun the Pi 4 `raspi4b` lane if the marker path remains shared
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-21-aarch64-psh-startup-visibility-scope.md`

## Notes

- Risks:
  keep the patch marker-only and do not turn it into a behavioral shell change
- Dependencies:
  completed `STEP-0232` `psh` startup visibility scope
- Source reminder:
  generic already echoes input but shows no shell response, so the next split
  must happen inside `psh` before any speculative shell fix
- User-visible control point before next step:
  after this step lands, the next implementation move should depend on the
  highest visible `psh` marker and should stay in the smallest subsystem that
  still matches that evidence
