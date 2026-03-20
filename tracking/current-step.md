# Current Step

## Metadata

- Step ID: `STEP-0243`
- Title: Implement bounded `psh_ttyopen()` failure visibility
- Status: `planned`
- Date: `2026-03-21`
- Milestone / phase: `Phase 1`

## Objective

- distinguish the first shared `psh_ttyopen("/dev/console")` failure reason on
  the generic and Pi 4 fast lanes

## Scope

In scope:

- add one bounded marker in the `psh` tty-open path
- rebuild the generic and Pi 4 fast lanes
- verify the first shared `psh_ttyopen()` error result

Out of scope:

- shell-policy changes
- console-device selection changes
- unrelated kernel or project changes
- Pi 5 or RP1 work
- real hardware work

## Expected Repositories

- `phoenix-rtos-utils`
- coordination repo

## Expected Files Or Subsystems

- `sources/phoenix-rtos-utils/psh/psh.c`
- `docs/status.md`
- `docs/source-artifacts.md`
- `manifests/`
- `tracking/current-step.md`
- `tracking/step-history.md`

## Acceptance Criteria

- generic QEMU reports the first bounded `psh_ttyopen()` failure result
- Pi 4 QEMU reports the same or a clearly different first bounded failure
- the result narrows the next move to one concrete tty-open follow-up

## Validation Plan

- Analysis only:
  not applicable
- Emulator:
  - rebuild generic `virt`
  - rerun generic QEMU
  - rerun Pi 4 QEMU
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-21-aarch64-root-dummyfs-fastlane.md`

## Notes

- Risks:
  keep the change limited to one read-only `psh` visibility marker and avoid
  mixing it with shell-policy or console-path changes
- Dependencies:
  completed `STEP-0242` `psh_ttyopen()` failure scoping
- Source reminder:
  `psh_run()` prints `psh: tty ready` only after `psh_ttyopen()` succeeds
- User-visible control point before next step:
  after this step lands, the next follow-up should depend on the first
  observed `psh_ttyopen()` error code
