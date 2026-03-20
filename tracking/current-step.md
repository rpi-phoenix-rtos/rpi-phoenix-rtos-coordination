# Current Step

## Metadata

- Step ID: `STEP-0174`
- Title: Scope single-core handoff experiment for the generic target
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- determine whether the next highest-signal step should be a controlled secondary-core containment experiment in the generic loader, given that the generic kernel target still declares `NUM_CPUS 1U`

## Scope

In scope:

- review the generic single-core assumptions and handoff behavior:
  - `phoenix-rtos-kernel/hal/aarch64/generic/config.h`
  - `plo/hal/aarch64/generic/_init.S`
- decide whether the next bounded step should keep non-boot CPUs trapped for the current generic target during handoff
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

- generic single-core target assumptions
- generic loader secondary-core handoff notes
- Pi 4 QEMU post-`eret` boundary notes
- manifests and tracking updates for this planning step

## Acceptance Criteria

- the generic `virt -smp 4` result is reflected in the scoped next step
- the next implementation step is narrowed to either:
  - a controlled secondary-core containment experiment
  - or an earliest-kernel-entry visibility change
- the scoped next step is specific enough to justify that choice

## Validation Plan

- Review:
  inspect the generic kernel CPU-count assumption and the loader secondary-core release path
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
  avoid changing secondary-core behavior without grounding it in the current single-core generic target assumptions
- Dependencies:
  completed `STEP-0173` generic multi-core loader handoff validation
- User-visible control point before next step:
  after this step lands, the next bounded move should be justified either as a single-core containment experiment or as a tiny earliest-kernel-entry probe, based on the documented target assumptions
