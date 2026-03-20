# Current Step

## Metadata

- Step ID: `STEP-0107`
- Title: Stage `loader.disk` for Pi 4 firmware preload at the generic `ram0` address
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- implement the smallest project-local Pi 4 boot-tree change that preloads the existing generic `loader.disk` payload for a firmware-booted `kernel8.img`

## Scope

In scope:

- update `phoenix-rtos-project/_projects/aarch64a53-generic-rpi4b/build.project`
- stage `loader.disk` into the Pi 4 firmware-facing boot tree
- update the project-local Pi 4 `config.txt` so firmware loads that payload to `0x48000000`
- keep the generic `ram0` / `loader.disk` contract intact

Out of scope:

- broad Pi 4 storage-driver work
- generic loader entry refactors beyond the now-validated multi-EL patch
- kernel Pi 4 driver enablement
- DTB staging policy changes beyond compatibility with the new boot-tree payload staging
- real-hardware-only validation
- Pi 5 or RP1 work
- `phoenix-rtos-tests` integration

## Expected Repositories

- coordination repo
- `phoenix-rtos-project`

## Expected Files Or Subsystems

- `phoenix-rtos-project/_projects/aarch64a53-generic-rpi4b/build.project`
- `phoenix-rtos-project/_projects/aarch64a53-generic-rpi4b/config.txt`
- `_boot/aarch64a53-generic-rpi4b/rpi4b/`
- manifests and tracking updates for this implementation step

## Acceptance Criteria

- the Pi 4 project stages `loader.disk` into the firmware-facing boot tree
- the Pi 4 `config.txt` explicitly loads that payload to `0x48000000`
- the staged `loader.disk` size fits within generic `RAM_BANK_SIZE`
- the default Pi 4 build still succeeds after the staging change

## Validation Plan

- Review:
  inspect the staging logic for consistency with the existing Pi 4 project-local boot-tree code
- Build:
  run the Pi 4 project build
- Emulator:
  not required
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-rpi4b-payload-staging-scope.md`

## Notes

- Risks:
  the step must stay project-local and must not silently widen into firmware protocol changes or new loader devices
- Dependencies:
  completed planning step `STEP-0106`
- User-visible control point before next step:
  after this step lands, the next bounded follow-up should come from the resulting Pi 4 boot-tree state, likely either DTB/config compatibility cleanup or the first hardware boot attempt
