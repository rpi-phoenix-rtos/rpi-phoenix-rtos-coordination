# Current Step

## Metadata

- Step ID: `STEP-0210`
- Title: Scope the Pi 4 local prescaler experiment
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- select the smallest next local interrupt controller follow-up now that the
  route-enable write alone is proven insufficient on the Pi 4 A72 lane

## Scope

In scope:

- review the completed local-route-enable results
- review the remaining Circle local interrupt controller setup
- decide whether the next one-variable experiment should be the local
  prescaler write or another equally narrow local-block setting
- keep the selected follow-up limited to one additional local-block variable

Out of scope:

- implementing the next local-block experiment in this planning step
- scheduler or VM changes
- broad interrupt-controller redesign
- Pi 5 or RP1 work

## Expected Repositories

- coordination repo
- coordination repo

## Expected Files Or Subsystems

- Pi 4 timer registration evidence after the restore
- Circle local-interrupt reference paths
- `external/circle/lib/sysinit.cpp`
- `external/circle/include/circle/bcm2836.h`
- completed Pi 4 local-route-enable evidence
- manifests and tracking updates for this implementation step

## Acceptance Criteria

- the next runtime hypothesis is narrowed to one concrete prescaler or local
  block follow-up
- the selected follow-up names the intended register, value, files, and
  validation evidence
- no new implementation work is mixed into this planning step

## Validation Plan

- Review:
  compare the completed route-enable result with Circle's remaining local block
  setup
- Build:
  not applicable
- Emulator:
  not applicable
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-rpi4b-local-interrupt-routing.md`

## Notes

- Risks:
  do not widen this into emulator-theory work or full local-controller support
- Dependencies:
  completed `STEP-0209` local interrupt routing experiment
- Source reminder:
  official Raspberry Pi kernel DTS files on `rpi-6.19.y` and `rpi-7.0.y` are currently identical for Pi 4 and keep `memory@0` bootloader-filled plus `stdout-path` on `serial1` (aux UART); Raspberry Pi documentation also confirms that firmware applies overlays and `dtparam`s before handing the merged DTB to the OS; this step specifically targets the root memory-node cell layout, not UART alias handling
- Architecture reminder:
  Raspberry Pi 4 Model B is based on BCM2711 with a quad-core Cortex-A72 CPU; treat `aarch64a53-generic-rpi4b` only as a temporary diagnostic lane and keep new target work centered on `aarch64a72-generic-rpi4b`
- User-visible control point before next step:
  after this scope lands, the next bounded move should change exactly one
  remaining local-block variable
