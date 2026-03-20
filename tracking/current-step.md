# Current Step

## Metadata

- Step ID: `STEP-0043`
- Title: Define first validated public common AArch64 timer wrapper step
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- choose the smallest validated path for the first public common AArch64 `hal_timer*` wrapper step without switching the current ZynqMP target prematurely

## Scope

In scope:

- inspect the new public timer-implementation hook after `STEP-0042`
- inspect the current AArch64 timer entry points and selection constraints
- choose the smallest file set and validation lane for the first public common AArch64 timer wrapper step

Out of scope:

- adding a new QEMU target
- implementing the common public `hal_timer*` wrapper file itself
- changing any selected runtime timer implementation
- adding PL011 console code
- Raspberry Pi-specific code

## Expected Repositories

- `phoenix-rtos-kernel`
- coordination repo

## Expected Files Or Subsystems

- `hal/aarch64/Makefile`
- `hal/aarch64/zynqmp/Makefile`
- `hal/aarch64/zynqmp/timer.c`
- tracking files and manifest updates for this step

## Acceptance Criteria

- the next public common AArch64 timer wrapper step is scoped explicitly
- the selected next step has a clear validation lane and exact file set
- the selected next step avoids premature target switching or uncontrolled multi-repo expansion

## Validation Plan

- Build:
  not applicable for this planning step
- Emulator:
  not applicable
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-public-timer-hook.md`

## Notes

- Risks:
  the first public wrapper step can easily sprawl into build, target, and runtime changes at once; the main job here is to stop that from happening
- Dependencies:
  completed implementation step `STEP-0042`
- User-visible control point before next step:
  after this planning step lands, the next implementation slice should be one narrow public-wrapper step with a specific validation strategy
