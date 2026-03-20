# Current Step

## Metadata

- Step ID: `STEP-0171`
- Title: Scope first assembly-side Pi 4 EL-exit visibility step
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- define the smallest next visibility step that distinguishes whether the Pi 4 official-DTB lane blocks inside `hal_exitToEL1()` before `eret`, at the `eret` handoff itself, or only in the first instructions of the kernel after the EL transition

## Scope

In scope:

- review the narrow assembly handoff path:
  - `plo/hal/aarch64/generic/_init.S`
  - the earliest kernel-side AArch64 entry path in `phoenix-rtos-kernel/hal/aarch64/_init.S`
- select one bounded visibility step that exposes the first silent boundary after `hal: jump exit el1`
- update manifests and docs with the scoped next step

Out of scope:

- code changes
- changing Pi 4 image layout
- changing DTB content or selection
- broader kernel instrumentation
- real-hardware-only validation
- Pi 5 or RP1 work
- `phoenix-rtos-tests` integration

## Expected Repositories

- coordination repo

## Expected Files Or Subsystems

- `plo` assembly EL-exit notes
- earliest kernel entry notes
- Pi 4 QEMU jump-path boundary notes
- manifests and tracking updates for this planning step

## Acceptance Criteria

- the reviewed assembly handoff path is explicitly recorded
- the next implementation step is narrowed to one assembly-side or earliest-entry visibility change
- the scoped next step is specific enough to divide `hal_exitToEL1()` itself from the first kernel instructions after the EL transition

## Validation Plan

- Review:
  inspect `hal_exitToEL1()` and the earliest kernel entry path
- Build:
  not applicable
- Emulator:
  not applicable
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-rpi4b-jump-visibility.md`

## Notes

- Risks:
  avoid widening into broad kernel instrumentation before the assembly handoff is explicitly split
- Dependencies:
  completed `STEP-0170` filtered Pi 4 `hal_cpuJump()` visibility
- User-visible control point before next step:
  after this step lands, the next bounded move should be a single assembly-side handoff visibility patch rather than a broad Pi 4 kernel change
