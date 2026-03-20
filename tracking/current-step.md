# Current Step

## Metadata

- Step ID: `STEP-0105`
- Title: Implement generic `plo` multi-EL entry and kernel handoff support
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- implement the smallest loader-local change that lets generic `plo` start and hand off to the kernel from EL1, EL2, or EL3

## Scope

In scope:

- update `plo/hal/aarch64/generic/_init.S`
- add EL-aware startup handling for EL1, EL2, and EL3
- add the matching EL-aware `hal_exitToEL1` handling
- preserve the existing EL3 path while unblocking the generic non-EL3 QEMU lanes

Out of scope:

- MMU refactoring for the generic loader
- board-specific Raspberry Pi drivers or DT parsing
- kernel Pi 4 enablement beyond reaching the existing generic handoff path
- DTB staging policy changes
- real-hardware-only validation
- Pi 5 or RP1 work
- `phoenix-rtos-tests` integration

## Expected Repositories

- coordination repo
- `plo`

## Expected Files Or Subsystems

- `plo/hal/aarch64/generic/_init.S`
- generic QEMU `virt` validation notes for EL1, EL2, and EL3 entry modes
- manifests and tracking updates for this implementation step

## Acceptance Criteria

- the generic loader builds cleanly after the `_init.S` change
- `virt,secure=on` still shows visible loader and kernel output
- `virt,secure=off` now shows visible loader and kernel output instead of hanging before the first banner
- `virt,secure=off,virtualization=on` now shows visible loader and kernel output instead of hanging before the first banner

## Validation Plan

- Review:
  inspect the EL dispatch and handoff logic for minimality and nearby-style consistency
- Build:
  rebuild the generic QEMU project and the Pi 4 scaffold project
- Emulator:
  run the generic QEMU image in EL3, EL1, and EL2 entry modes
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-generic-plo-entry-scope.md`

## Notes

- Risks:
  the patch must stay localized to the generic loader entry assembly and must not widen into broader generic-loader or kernel architecture work
- Dependencies:
  completed planning step `STEP-0104`
- User-visible control point before next step:
  after this patch lands, the next bounded decision should be taken from the new three-mode QEMU runtime state rather than from more speculative Pi 4 work
