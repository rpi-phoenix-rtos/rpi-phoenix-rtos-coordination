# Current Step

## Metadata

- Step ID: `STEP-0025`
- Title: Define the first CPU0-directed timer wakeup-notification step
- Status: `ready`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- define the first narrow timer-path change that will let non-CPU0 contexts request CPU0 wakeup-timer reprogramming once the common architectural timer backend starts to become real

## Scope

In scope:

- inspect the current AArch64 SGI and timer preparation state after `STEP-0024`
- determine how a timer-update notification should reserve or reuse an interrupt number
- determine which component should own the first CPU0-directed notification handler
- select the smallest exact file set for the first runtime-oriented timer-notification patch
- keep this as a planning and scoping step only

Out of scope:

- adding a new QEMU target
- implementing the timer-notification path itself
- changing timer or scheduler runtime behavior
- implementing the common generic timer runtime backend itself
- adding PL011 console code
- Raspberry Pi-specific code

## Expected Repositories

- coordination repo
- likely `phoenix-rtos-kernel`

## Expected Files Or Subsystems

- `hal/aarch64/interrupts_gicv2.c`
- `hal/timer.h`
- `proc/threads.c`
- tracking files and manifest updates for the chosen next step

## Acceptance Criteria

- the first CPU0-directed timer wakeup-notification step is explicitly scoped with exact touched files, ownership, interrupt-number strategy, and validation command
- the selected next step is narrow enough to implement and validate in one controlled follow-up session

## Validation Plan

- Build:
  not applicable
- Emulator:
  not applicable
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-targeted-sgi-helper.md`

## Notes

- Risks:
  the next runtime step now depends on an explicit SGI reservation and handler-ownership decision rather than on another isolated helper function
- Dependencies:
  completed targeted-SGI helper step from `STEP-0024`
- User-visible control point before next step:
  the next runtime-oriented code change should not begin until this step records the exact SGI/notification contract to use
