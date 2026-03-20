# Current Step

## Metadata

- Step ID: `STEP-0122`
- Title: Scope the smallest `pl011-tty` console-readiness implementation step
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- choose the smallest shared implementation step that can move both the generic QEMU lane and the Pi 4 DTB-backed QEMU lane beyond `pl011-tty: started`

## Scope

In scope:

- inspect the current `pl011-tty` registration path and the already documented generic boundary around `create_dev()`
- compare the generic and Pi 4 DTB-backed runtime evidence
- select one small follow-up patch in shared userspace/device code if possible
- keep the next step bounded to console readiness rather than broad shell or devfs redesign

Out of scope:

- broad Pi 4 peripheral-debug work
- new board-specific DTB work unless the console-readiness review proves it is still implicated
- broad init-sequencing redesign across loader scripts and services
- real-hardware-only validation
- Pi 5 or RP1 work
- `phoenix-rtos-tests` integration
- unrelated kernel early-console work unless it is shown to be smaller than the shared `pl011-tty` fix

## Expected Repositories

- coordination repo

## Expected Files Or Subsystems

- `sources/phoenix-rtos-devices/tty/pl011-tty/pl011-tty.c`
- relevant generic and Pi 4 QEMU smoke notes
- manifests and tracking updates for this planning step

## Acceptance Criteria

- the next patch target is explicit
- the patch is small enough for one reviewable implementation step
- the choice is backed by the current shared runtime boundary rather than by speculation

## Validation Plan

- Review:
  inspect the current `pl011-tty` registration path and compare it with the existing runtime diagnostics
- Build:
  not required
- Emulator:
  use the completed generic and Pi 4 QEMU results as the runtime basis for step selection
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-console-readiness-scope.md`

## Notes

- Risks:
  do not widen this into a large init-order investigation before the smallest shared `pl011-tty` step is identified
- Dependencies:
  completed `STEP-0121`
- User-visible control point before next step:
  after this step lands, the next bounded move should be one small implementation patch in the shared console path
