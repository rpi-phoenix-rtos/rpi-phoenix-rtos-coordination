# Current Step

## Metadata

- Step ID: `STEP-0253`
- Title: Scope the smallest pre-`psh` `/dev` namespace fix
- Status: `planned`
- Date: `2026-03-21`
- Milestone / phase: `Phase 1`

## Objective

- identify the smallest fast-lane image or startup change that makes `/dev`
  resolvable before the shell opens `/dev/console`

## Scope

In scope:

- inspect existing Phoenix project patterns for pre-shell `devfs` binding
- inspect whether `psh` applets can provide a minimal pre-shell `mkdir` and
  `bind` path without widening into new utilities
- select one small project-local fix

Out of scope:

- kernel name-resolution changes
- libphoenix path-resolution changes
- `psh` console-policy changes
- unrelated Pi 4 peripheral work
- real hardware work

## Expected Repositories

- coordination repo
- `sources/phoenix-rtos-project`

## Expected Files Or Subsystems

- `sources/phoenix-rtos-project/_projects/aarch64a53-generic-qemu/*`
- `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/*`
- `sources/phoenix-rtos-filesystems/dummyfs/srv.c`
- `sources/phoenix-rtos-utils/psh/*`
- `docs/status.md`
- `docs/source-artifacts.md`
- `manifests/`
- `tracking/current-step.md`
- `tracking/step-history.md`

## Acceptance Criteria

- one concrete smallest fix is selected for making `/dev` visible before the
  shell starts
- the selected fix stays in project composition or startup wiring
- the result is captured in one manifest and the next implementation step

## Validation Plan

- Analysis:
  source review of `dummyfs`, `psh`, and current project files
- Emulator:
  not required unless the source review leaves one ambiguity about applet or
  startup behavior
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-21-aarch64-resolve-path-dev-seam.md`

## Notes

- Risks:
  avoid widening into shell redesign when the current issue appears to be
  namespace setup
- Dependencies:
  completed `STEP-0252` `/dev` resolve-path seam analysis
- Source reminder:
  both the `stat()` and direct `open()` path-resolution calls fail at
  intermediate `/dev`, not at `console`
- User-visible control point before next step:
  after this scope step lands, the next step should be a project-local startup
  change, not another kernel or libc probe
