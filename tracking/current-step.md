# Current Step

## Metadata

- Step ID: `STEP-0058`
- Title: Define first emulated generic AArch64 smoke-lane step
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- identify the smallest runtime verification step for the new `aarch64a53-generic-qemu` entry point on QEMU `virt`

## Scope

In scope:

- inspect the new generic QEMU boot artifacts and launch script
- choose the first bounded runtime command and the first success signal to look for
- keep the step planning-only and stop before runtime or code changes

Out of scope:

- implementation code in upstream Phoenix repositories
- `phoenix-rtos-tests` target additions
- Raspberry Pi-specific code
- runtime bug fixing in this planning step

## Expected Repositories

- coordination repo

## Expected Files Or Subsystems

- `phoenix-rtos-project/_targets/aarch64a53/generic-qemu/`
- `phoenix-rtos-project/_projects/aarch64a53-generic-qemu/`
- `phoenix-rtos-project/scripts/aarch64a53-generic-qemu.sh`
- `docs/status.md`
- tracking files and manifest updates for this step
- `docs/status.md`
- tracking files and manifest updates for this step

## Acceptance Criteria

- the selected smoke step names the exact runtime command to execute first
- the selected smoke step defines the first accepted boot evidence for success or failure
- the result explains why that smoke step comes before the emulated test-target integration

## Validation Plan

- Review:
  inspect the current launch script, artifact names, and generic `plo`/kernel constraints
- Build:
  not applicable
- Emulator:
  not applicable
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-generic-qemu-project-entry.md`

## Notes

- Risks:
  the result must stay as one smoke-step selection and must not silently turn into runtime debugging or cross-repo build unblock work
- Dependencies:
  completed implementation step `STEP-0057`
- User-visible control point before next step:
  after this planning step lands, the next slice should be the selected first emulated smoke run
