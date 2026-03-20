# Current Step

## Metadata

- Step ID: `STEP-0102`
- Title: Define the first optional Pi 4 DTB staging hook
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- identify the smallest project-local follow-up that can make Pi 4 DTB staging possible without forcing an external DTB file into every build

## Scope

In scope:

- inspect the new Pi 4 boot-tree staging result
- choose the smallest project-local DTB staging hook that keeps builds reproducible without requiring a checked-in external DTB blob
- stop before implementing that hook

Out of scope:

- DTB generation from external trees
- broad loader or kernel Pi 4 support
- real-hardware-only validation
- Pi 5 or RP1 work
- `phoenix-rtos-tests` target additions

## Expected Repositories

- coordination repo
- `phoenix-rtos-project`

## Expected Files Or Subsystems

- `phoenix-rtos-project/_projects/aarch64a53-generic-rpi4b/*`
- `_boot/aarch64a53-generic-rpi4b/rpi4b/`
- Pi 4 boot-staging documentation and manifest updates
- `docs/status.md`
- tracking files and manifest updates for this step

## Acceptance Criteria

- the next DTB-related follow-up is selected from the staged-boot-tree state
- the follow-up stays as one small implementation commit where possible
- the selected step keeps the Pi 4 lane project-local and build-safe

## Validation Plan

- Review:
  inspect the staged boot-tree result and documented Pi 4 DTB expectations together
- Build:
  use the current project-local build shape to choose the smallest useful DTB follow-up
- Emulator:
  not applicable
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-rpi4b-firmware-staging.md`

## Notes

- Risks:
  the result must stay as a localized planning step and must not silently widen into broad firmware-asset bundling or loader handoff work
- Dependencies:
  completed implementation step `STEP-0101`
- User-visible control point before next step:
  after the next DTB hook is selected, the implementation should stay project-local unless a stronger dependency emerges
