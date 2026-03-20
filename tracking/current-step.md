# Current Step

## Metadata

- Step ID: `STEP-0027`
- Title: Define the first source-agnostic AArch64 timer helper step
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- define the first narrow common AArch64 architectural timer helper step that removes the physical-versus-virtual timer sysreg split from future backend code

## Scope

In scope:

- inspect the current AArch64 architectural timer helpers and DTB source-selection API after `STEP-0026`
- choose the first source-agnostic helper shape for physical-versus-virtual timer operations
- select the smallest exact touched-file set for that helper step
- keep this as a planning and scoping step only

Out of scope:

- adding a new QEMU target
- implementing the selected helper step itself
- changing the timer backend implementation itself
- implementing the common generic timer runtime backend itself
- adding PL011 console code
- Raspberry Pi-specific code

## Expected Repositories

- coordination repo
- likely `phoenix-rtos-kernel`

## Expected Files Or Subsystems

- `hal/aarch64/aarch64.h`
- `hal/aarch64/dtb.h`
- possibly a new common AArch64 timer-helper header
- tracking files and manifest updates for the chosen next step

## Acceptance Criteria

- the first source-agnostic AArch64 timer helper step is explicitly scoped with exact touched files, rationale, validation command, and success criteria
- the selected helper step is narrow enough to implement and validate in one controlled follow-up session

## Validation Plan

- Build:
  not applicable
- Emulator:
  not applicable
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-cpu0-wakeup-timer-notification.md`

## Notes

- Risks:
  the next step must not turn into a full generic backend or duplicate the backend policy already encoded in the DTB API
- Dependencies:
  completed CPU0 wakeup-notification step from `STEP-0026`
- User-visible control point before next step:
  the next code change should introduce only the smallest source-agnostic timer helper layer, not the full backend
