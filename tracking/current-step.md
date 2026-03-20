# Current Step

## Metadata

- Step ID: `STEP-0103`
- Title: Add the first optional Pi 4 DTB staging hook
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- add the smallest project-local DTB staging hook that allows a Pi 4 board DTB to be staged when provided, while keeping the default build self-contained

## Scope

In scope:

- update `phoenix-rtos-project/_projects/aarch64a53-generic-rpi4b/build.project`
- accept an optional externally provided Pi 4 DTB path or project-local DTB file
- stage the DTB into `_boot/aarch64a53-generic-rpi4b/rpi4b/` only when available
- keep the no-hardware default build green when no DTB is supplied

Out of scope:

- DTB generation from external trees
- checked-in imported Linux DTB blobs
- broad loader or kernel Pi 4 support
- real-hardware-only validation
- Pi 5 or RP1 work
- `phoenix-rtos-tests` target additions

## Expected Repositories

- coordination repo
- `phoenix-rtos-project`

## Expected Files Or Subsystems

- `phoenix-rtos-project/_projects/aarch64a53-generic-rpi4b/build.project`
- `_boot/aarch64a53-generic-rpi4b/rpi4b/`
- Pi 4 boot-staging documentation and manifest updates
- `docs/status.md`
- `docs/manual-operator-instructions.md`
- tracking files and manifest updates for this step

## Acceptance Criteria

- the Pi 4 project stages a board DTB when one is explicitly provided
- the default no-DTB build still succeeds and still stages the existing boot tree
- the DTB hook remains project-local and does not add a required external dependency to every build

## Validation Plan

- Review:
  inspect the DTB hook for minimality and optional behavior
- Build:
  run the Pi 4 build once without a DTB to confirm the default lane still works
- Emulator:
  not applicable
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-rpi4b-dtb-hook-scope.md`

## Notes

- Risks:
  the result must stay as one localized DTB-staging step and must not silently widen into external-tree fetching or loader handoff work
- Dependencies:
  completed planning step `STEP-0102`
- User-visible control point before next step:
  after the DTB hook lands, the next follow-up should be chosen from the remaining firmware-handoff blocker or from concrete DTB consumption needs
