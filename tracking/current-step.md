# Current Step

## Metadata

- Step ID: `STEP-0174`
- Title: Scope earliest kernel-entry visibility on Pi 4
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- define the smallest next visibility step that distinguishes whether the Pi 4 official-DTB lane reaches the first instructions of kernel `_start` after the EL3 `eret`

## Scope

In scope:

- review the earliest kernel AArch64 entry path:
  - `phoenix-rtos-kernel/hal/aarch64/_init.S`
  - any generic early console assumptions needed for a tiny raw marker
- select one bounded earliest-entry visibility step
- update manifests and docs with the scoped next step

Out of scope:

- code changes
- changing Pi 4 image layout
- changing DTB content or selection
- semantic EL-handoff changes
- real-hardware-only validation
- Pi 5 or RP1 work
- `phoenix-rtos-tests` integration

## Expected Repositories

- coordination repo

## Expected Files Or Subsystems

- earliest kernel entry notes
- Pi 4 QEMU post-`eret` boundary notes
- manifests and tracking updates for this planning step

## Acceptance Criteria

- the generic `virt -smp 4` result is reflected in the scoped next step
- the next implementation step is narrowed to one earliest-kernel-entry visibility change
- the scoped next step is specific enough to divide post-`eret` failure from later kernel initialization

## Validation Plan

- Review:
  inspect the earliest kernel `_start` path and its feasibility for a tiny raw marker
- Build:
  not applicable
- Emulator:
  not applicable
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-generic-qemu-smp4-handoff-validation.md`

## Notes

- Risks:
  avoid widening into large kernel instrumentation instead of a tiny earliest-entry probe
- Dependencies:
  completed `STEP-0173` generic multi-core loader handoff validation
- User-visible control point before next step:
  after this step lands, the next bounded move should be a tiny earliest-kernel-entry probe rather than a speculative secondary-core containment change
