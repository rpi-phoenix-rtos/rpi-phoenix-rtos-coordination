# Current Step

## Metadata

- Step ID: `STEP-0113`
- Title: Stage operator-supplied Raspberry Pi firmware files into the Pi 4 boot tree
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- implement the smallest project-local step that lets the Pi 4 staged boot tree include operator-supplied Raspberry Pi firmware files

## Scope

In scope:

- update `phoenix-rtos-project/_projects/aarch64a53-generic-rpi4b/build.project`
- add an optional firmware-directory input for the Pi 4 project
- copy the supplied Raspberry Pi firmware files into `_boot/aarch64a53-generic-rpi4b/rpi4b/`
- keep default no-firmware builds green

Out of scope:

- broad Pi 4 storage-driver work
- changes to the already validated Pi 4 `plo` MMIO override path
- kernel Pi 4 driver enablement
- bundling a DTB source into the repo
- FAT image packaging or SD writing automation
- real-hardware-only validation
- Pi 5 or RP1 work
- `phoenix-rtos-tests` integration

## Expected Repositories

- coordination repo
- `phoenix-rtos-project`

## Expected Files Or Subsystems

- `phoenix-rtos-project/_projects/aarch64a53-generic-rpi4b/build.project`
- staged `_boot/aarch64a53-generic-rpi4b/rpi4b/` contents
- `docs/manual-operator-instructions.md`
- manifests and tracking updates for this implementation step

## Acceptance Criteria

- the Pi 4 project accepts an operator-supplied firmware directory input
- when that input is supplied, the expected firmware files are staged into `_boot/aarch64a53-generic-rpi4b/rpi4b/`
- default no-firmware builds remain green

## Validation Plan

- Review:
  inspect the firmware staging path for minimality and confirm it does not interfere with ordinary no-hardware builds
- Build:
  run the Pi 4 project build
- Emulator:
  not required
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-rpi4b-firmware-file-staging-scope.md`

## Notes

- Risks:
  the step must stay at artifact staging only and must not silently widen into FAT-image generation, media writing, or bootloader policy changes
- Dependencies:
  completed planning step `STEP-0112`
- User-visible control point before next step:
  after this step lands, the next bounded decision should come from whether to package the staged Pi 4 boot tree into a reproducible FAT image or move directly to the first real-board smoke attempt
