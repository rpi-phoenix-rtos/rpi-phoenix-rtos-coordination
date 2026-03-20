# Current Step

## Metadata

- Step ID: `STEP-0104`
- Title: Define the first generic loader entry step for Pi 4 firmware handoff
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- bound the smallest useful `plo` change that removes the current EL3-only boot-entry assumption from the Pi 4 path while preserving the existing generic and ZynqMP validation lanes

## Scope

In scope:

- inspect generic AArch64 `plo` entry and exit paths in `hal/aarch64/generic/_init.S`
- confirm the exact EL3-only assumptions that currently block Raspberry Pi firmware boot
- define the smallest next implementation step and its validation lane
- keep the selected next step narrow enough to remain loader-local

Out of scope:

- broad Raspberry Pi-specific driver work
- kernel Pi 4 enablement
- DTB import policy changes beyond the now-completed staging hook
- real-hardware-only validation
- Pi 5 or RP1 work
- changes to `phoenix-rtos-tests`

## Expected Repositories

- coordination repo

## Expected Files Or Subsystems

- `plo/hal/aarch64/generic/_init.S`
- `plo/hal/aarch64/generic/hal.c`
- generic QEMU `virt` validation notes
- documentation and manifest updates for this planning step

## Acceptance Criteria

- the current generic loader EL3-only entry and exit constraints are explicitly documented from source inspection
- the next implementation step is fixed as one bounded loader-local change rather than a broad bring-up bucket
- the chosen validation path is defined and includes a non-hardware lane if feasible

## Validation Plan

- Review:
  inspect `plo` generic entry and exit assembly and the current generic QEMU invocation
- Build:
  not applicable
- Emulator:
  define whether QEMU `virt,secure=off` can serve as the first EL2-style validation lane
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-rpi4b-dtb-hook.md`

## Notes

- Risks:
  the next implementation step must not silently widen into full Raspberry Pi bootloader support, DTB parsing, or kernel work
- Dependencies:
  completed implementation step `STEP-0103`
- User-visible control point before next step:
  once the first loader-entry step is defined, the next action should be a single `plo` implementation patch that can be validated in builds and, if possible, in generic QEMU
