# Current Step

## Metadata

- Step ID: `STEP-0017`
- Title: Define the first common AArch64 generic timer backend step
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- define and bound the first common AArch64 generic timer backend step now that the DTB timer metadata, timer IRQ HAL seam, and timer sysreg helpers exist

## Scope

In scope:

- inspect the current AArch64 timer-related preparation work in `phoenix-rtos-kernel`
- choose the first generic timer backend shape:
  - common helper backend
  - directly selectable common timer implementation
- choose the first timer source policy for arm64 generic bring-up
- select one small next implementation step with exact touched files and validation lane
- keep this as a planning and scoping step only

Out of scope:

- adding a new QEMU target
- implementing the selected generic timer backend step itself
- adding PL011 console code
- Raspberry Pi-specific code

## Expected Repositories

- coordination repo
- likely `phoenix-rtos-kernel`

## Expected Files Or Subsystems

- `hal/aarch64/aarch64.h`
- `hal/aarch64/dtb.c`
- `hal/timer.h`
- likely a new common AArch64 timer source file or header
- tracking files and manifest updates for the chosen next step

## Acceptance Criteria

- the first generic AArch64 timer backend step is explicitly scoped with exact touched files, rationale, validation command, and success criteria
- the selected next step is narrow enough to implement and validate in one controlled follow-up session

## Validation Plan

- Build:
  not applicable for this planning step
- Emulator:
  inspect current timer source assumptions as needed to choose the narrowest backend step
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-timer-sysreg-helpers.md`

## Notes

- Risks:
  the first generic timer backend is the first substantial new AArch64 runtime code path after the DTB preparation series, so it must be explicitly bounded before it is introduced
- Dependencies:
  completed DTB timer metadata work, timer IRQ HAL split, and timer sysreg helper step
- User-visible control point before next step:
  present the exact selected generic timer backend step before introducing the first common AArch64 timer implementation
