# Current Step

## Metadata

- Step ID: `STEP-0111`
- Title: Make generic `plo` MMIO base addresses board-overridable for Pi 4
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- implement the smallest loader-side step that stops Pi 4 `kernel8.img` from assuming QEMU `virt` UART and GIC base addresses

## Scope

In scope:

- update `plo/hal/aarch64/generic/config.h`
- update `phoenix-rtos-project/_projects/aarch64a53-generic-rpi4b/board_config.h`
- add optional board-config override macros for generic `plo` UART0, GIC distributor, and GIC CPU-interface base addresses
- preserve the current QEMU `virt` defaults when no board overrides are supplied

Out of scope:

- broad Pi 4 storage-driver work
- DTB propagation, which is now complete
- kernel Pi 4 driver enablement
- runtime Pi 4 timer, mailbox, or framebuffer support
- firmware policy changes unrelated to `plo` MMIO addressing
- real-hardware-only validation
- Pi 5 or RP1 work
- `phoenix-rtos-tests` integration

## Expected Repositories

- coordination repo
- `plo`
- `phoenix-rtos-project`

## Expected Files Or Subsystems

- `plo/hal/aarch64/generic/config.h`
- `phoenix-rtos-project/_projects/aarch64a53-generic-rpi4b/board_config.h`
- generic QEMU `plo` smoke lane
- Pi 4 project-local build artifacts
- manifests and tracking updates for this implementation step

## Acceptance Criteria

- generic `plo` now accepts optional board-config MMIO base overrides for UART0 and GICv2
- the generic QEMU build and smoke lane remain green with default values
- the Pi 4 project now compiles `plo` with Pi 4-specific MMIO addresses instead of the generic QEMU hardcoded set

## Validation Plan

- Review:
  inspect the override path for minimality and confirm that the QEMU default path is still the fallback
- Build:
  run both the generic QEMU and Pi 4 project builds
- Emulator:
  rerun the known-good generic QEMU smoke lane
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-rpi4b-plo-address-override-scope.md`

## Notes

- Risks:
  the step must stay at the config/address-override layer and must not silently widen into timer, DTB parsing, or full Pi 4 interrupt-controller bring-up
- Dependencies:
  completed planning step `STEP-0110`
- User-visible control point before next step:
  after this step lands, the next bounded decision should come from the remaining first-boot blockers in the Pi 4 loader or kernel path rather than from more artifact-only staging work
