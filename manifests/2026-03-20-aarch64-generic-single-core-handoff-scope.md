# Manifest: Generic Single-Core Handoff Experiment Scope

- Date: `2026-03-20`
- Step: `STEP-0174`
- Status: `completed`

## Goal

- decide whether the next highest-signal Pi 4 follow-up should be a loader-side secondary-core containment experiment or an earliest-kernel-entry visibility change

## Changes

No code changes.

## Review Basis

Reviewed:

- `sources/phoenix-rtos-kernel/hal/aarch64/generic/config.h`
- `sources/plo/hal/aarch64/generic/_init.S`
- `manifests/2026-03-20-aarch64-generic-qemu-smp4-handoff-validation.md`

## Findings

- the current generic kernel target still declares `NUM_CPUS 1U`
- the generic loader currently traps all non-boot CPUs only until `hal_coreJumpFlag` flips, then releases them into the same `hal_exitToEL1()` path as core 0
- generic `virt -smp 4` proves that repeated EL3 handoff markers are not a failure by themselves, but it does not remove the single-core design mismatch between the current generic kernel target and the loader handoff policy
- that mismatch is now the highest-signal narrow experiment, because it can be changed in one loader assembly site without widening into kernel instrumentation yet

## Conclusion

- the next bounded implementation step should be a controlled secondary-core containment experiment in generic `plo`
- the experiment should keep non-boot CPUs parked across the current generic handoff while preserving the existing core-0 path
- validate first on:
  - generic `virt -smp 4`
  - Pi 4 `raspi4b -smp 4` with the official firmware DTB

## Selected Next Step

- implement generic loader secondary-core containment across the current single-core handoff
