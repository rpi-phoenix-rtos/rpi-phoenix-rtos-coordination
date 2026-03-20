# Current Step

## Metadata

- Step ID: `STEP-0023`
- Title: Make AArch64 GICv2 handler registration PPI-safe
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- remove the SPI-shaped CPU-targeting assumption from common AArch64 GICv2 handler registration for non-SPI interrupts so a future PPI-backed architectural timer fits the interrupt layer cleanly

## Scope

In scope:

- update common AArch64 GICv2 handler registration so it applies CPU targeting only to SPI interrupts
- preserve existing SPI behavior and the current ZynqMP build lane
- validate the existing `aarch64a53-zynqmp-qemu` build in `phoenix-dev`

Out of scope:

- adding a new QEMU target
- implementing the common generic timer runtime backend itself
- changing timer wakeup semantics
- adding PL011 console code
- Raspberry Pi-specific code

## Expected Repositories

- `phoenix-rtos-kernel`
- coordination repo

## Expected Files Or Subsystems

- `hal/aarch64/interrupts_gicv2.c`
- tracking files and manifest updates for this step

## Acceptance Criteria

- common AArch64 GICv2 handler registration applies CPU targeting only to SPI interrupts
- the change is limited to interrupt-registration semantics and does not widen into a broader GIC rewrite
- the existing `aarch64a53-zynqmp-qemu` build still succeeds in `phoenix-dev`

## Validation Plan

- Build:
  refresh the copied buildroot and rebuild `TARGET=aarch64a53-zynqmp-qemu` with `./phoenix-rtos-build/build.sh clean host core project`
- Emulator:
  not applicable
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-timer-backend-selection-hook.md`

## Notes

- Risks:
  the step must not accidentally widen into per-CPU interrupt enablement or timer runtime behavior changes
- Dependencies:
  completed timer-backend selection step from `STEP-0022`
- User-visible control point before next step:
  after this fix lands, re-scope the next generic-timer runtime step against the remaining CPU-affine wakeup constraint instead of widening into a full architectural timer backend immediately
