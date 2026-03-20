# Current Step

## Metadata

- Step ID: `STEP-0056`
- Title: Define first `aarch64a53-generic-qemu` project entry-point step
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- identify the smallest `phoenix-rtos-project` step needed to turn the new generic AArch64 kernel and `plo` targets into a first runnable `aarch64a53-generic-qemu` entry point

## Scope

In scope:

- inspect the existing `phoenix-rtos-project` target layout, scripts, and boot artifacts around QEMU targets
- choose the first concrete generic project file set and runtime command
- keep the step planning-only and stop before implementation code

Out of scope:

- implementation code in upstream Phoenix repositories
- `phoenix-rtos-tests` target additions
- Raspberry Pi-specific code
- solving runtime boot bugs in this planning step

## Expected Repositories

- coordination repo

## Expected Files Or Subsystems

- `docs/status.md`
- tracking files and manifest updates for this step
- `phoenix-rtos-project/_targets/aarch64a53/generic-qemu/`
- `phoenix-rtos-project/_projects/aarch64a53-generic-qemu/`
- `phoenix-rtos-project/scripts/aarch64a53-generic-qemu.sh`
- `docs/status.md`
- tracking files and manifest updates for this step

## Acceptance Criteria

- the selected project step identifies the exact generic QEMU project files needed first
- the selected project step has a realistic first runtime command for QEMU `virt`
- the result explains why the project entry-point step should move before the emulated test target

## Validation Plan

- Review:
  inspect the existing project-side QEMU target layout and the new generic AArch64 boot components
- Build:
  not applicable
- Emulator:
  not applicable
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-plo-generic-scaffold.md`

## Notes

- Risks:
  the result must stay as one project-entry planning step and must not silently turn into a multi-repo implementation patch
- Dependencies:
  completed implementation step `STEP-0055`
- User-visible control point before next step:
  after this planning step lands, the next slice should be the selected first project implementation step for the generic QEMU lane
