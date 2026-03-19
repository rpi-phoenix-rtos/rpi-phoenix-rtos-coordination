# Current Step

## Metadata

- Step ID: `STEP-0016`
- Title: Add AArch64 architectural timer sysreg helpers
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- add the smallest reusable set of AArch64 architectural timer system-register helpers needed for a future generic ARM timer backend, while preserving the current `aarch64a53-zynqmp` behavior

## Scope

In scope:

- update `phoenix-rtos-kernel/hal/aarch64/aarch64.h`
- add helper accessors for the AArch64 architectural timer registers needed by a future generic timer backend
- keep the change small, header-only, and kernel-only
- validate that the existing `aarch64a53-zynqmp-qemu` build still succeeds after the helper addition

Out of scope:

- adding a new QEMU target
- adding generic ARM timer runtime code
- adding PL011 console code
- Raspberry Pi-specific code

## Expected Repositories

- `phoenix-rtos-kernel`
- coordination repo

## Expected Files Or Subsystems

- `sources/phoenix-rtos-kernel/hal/aarch64/aarch64.h`
- copied-buildroot AArch64 validation workflow
- tracking files and manifest updates after validation

## Acceptance Criteria

- reusable AArch64 helpers exist for the first architectural timer registers needed by a generic timer backend
- the change remains small and does not introduce a new runtime path yet
- `TARGET=aarch64a53-zynqmp-qemu ./phoenix-rtos-build/build.sh clean host core project` still succeeds inside `phoenix-dev` using the copied buildroot

## Validation Plan

- Build:
  refresh the copied buildroot, then run the existing `aarch64a53-zynqmp-qemu` build path in `phoenix-dev`
- Emulator:
  not applicable
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-timer-irq-api.md`

## Notes

- Risks:
  this is preparatory helper work only, not the generic ARM timer backend itself
- Dependencies:
  completed timer IRQ HAL split from `STEP-0015`
- User-visible control point before next step:
  present the exact helper set, validation command, and resulting commit before moving into generic timer runtime code
