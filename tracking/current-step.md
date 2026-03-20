# Current Step

## Metadata

- Step ID: `STEP-0101`
- Title: Stage the first Pi 4 firmware boot-tree artifacts
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- add the smallest project-local boot-tree staging step that produces firmware-visible Pi 4 boot artifacts from the current scaffold

## Scope

In scope:

- update `phoenix-rtos-project/_projects/aarch64a53-generic-rpi4b/build.project`
- stage a Pi 4 boot directory under `_boot/aarch64a53-generic-rpi4b/`
- add a project-local `config.txt`
- copy the raw `plo` image under a firmware-facing kernel filename
- document the remaining DTB and firmware-handoff blockers in the manifest

Out of scope:

- FAT image generation
- DTB generation or import
- broad loader or kernel Pi 4 support
- real-hardware-only validation
- Pi 5 or RP1 work
- `phoenix-rtos-tests` target additions

## Expected Repositories

- coordination repo
- `phoenix-rtos-project`

## Expected Files Or Subsystems

- `phoenix-rtos-project/_projects/aarch64a53-generic-rpi4b/build.project`
- `phoenix-rtos-project/_projects/aarch64a53-generic-rpi4b/config.txt`
- `_boot/aarch64a53-generic-rpi4b/rpi4b/`
- Pi 4 boot-staging documentation and manifest updates
- `docs/status.md`
- tracking files and manifest updates for this step

## Acceptance Criteria

- the Pi 4 project stages a firmware-facing boot directory in `_boot`
- the staged directory contains a project-local `config.txt` and a renamed raw `plo` image
- the no-hardware Pi 4 build lane still succeeds in `phoenix-dev`

## Validation Plan

- Review:
  inspect the staged artifact names and config choices against the documented Raspberry Pi firmware behavior
- Build:
  run `LIBPHOENIX_DEVEL_MODE=n TARGET=aarch64a53-generic-rpi4b ./phoenix-rtos-build/build.sh host core project image` in `phoenix-dev`
- Emulator:
  not applicable
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-rpi4b-firmware-staging-scope.md`

## Notes

- Risks:
  the result must stay as one localized boot-staging step and must not silently widen into DTB import or loader handoff work
- Dependencies:
  completed planning step `STEP-0100`
- User-visible control point before next step:
  after the staging step lands, the next follow-up should be selected from the staged artifact gap, not from broad board bring-up wishlisting
