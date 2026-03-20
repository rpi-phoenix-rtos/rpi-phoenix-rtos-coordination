# Current Step

## Metadata

- Step ID: `STEP-0100`
- Title: Define the first Pi 4 firmware-facing boot-staging step
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- identify the smallest project-local artifact step that moves the new Pi 4 scaffold toward a firmware-bootable SD/boot-partition layout

## Scope

In scope:

- inspect the newly validated Pi 4 project scaffold together with Raspberry Pi firmware boot requirements already documented in this repo
- choose the smallest next project-local staging step that can prepare firmware-visible boot artifacts without real hardware
- stop before implementing that staging step

Out of scope:

- broad loader or kernel Pi 4 support
- real-hardware-only validation
- Pi 5 or RP1 work
- `phoenix-rtos-tests` target additions

## Expected Repositories

- coordination repo
- `phoenix-rtos-project`

## Expected Files Or Subsystems

- `phoenix-rtos-project/_projects/aarch64a53-generic-rpi4b/*`
- `phoenix-rtos-project/_targets/aarch64a53/generic/*`
- Pi 4 boot-staging documentation and manifest updates
- `docs/status.md`
- tracking files and manifest updates for this step
- Raspberry Pi firmware boot requirements already documented in this repo

## Acceptance Criteria

- the next Pi 4 boot-staging step is selected from current scaffold and documented firmware facts
- the follow-up stays as one small implementation commit where possible
- the selected step directly improves the path to a first Pi 4 firmware -> `plo` boot attempt

## Validation Plan

- Review:
  inspect the Pi 4 scaffold outputs and documented firmware requirements together
- Build:
  use local project state and already-collected Raspberry Pi documentation to choose the smallest useful staging follow-up
- Emulator:
  not applicable
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-rpi4b-project-scaffold.md`

## Notes

- Risks:
  the result must stay as a localized planning step and must not silently widen into a broad firmware-packaging or loader-port batch
- Dependencies:
  completed implementation step `STEP-0099`
- User-visible control point before next step:
  after the next staging step is selected, implementation should stay project-local unless a stronger firmware or loader dependency emerges
