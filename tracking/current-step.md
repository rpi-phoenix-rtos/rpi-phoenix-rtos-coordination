# Current Step

## Metadata

- Step ID: `STEP-0202`
- Title: Analyze the external `rpi4-osdev` Pi 4 bring-up reference
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- review the external `sypstraw/rpi4-osdev` repository and extract the Pi 4
  bring-up details most likely to help the current Phoenix Pi 4 boot-first lane

## Scope

In scope:

- clone or otherwise inspect the repository contents
- focus on boot flow, exception levels, timer or interrupt setup, UART,
  MMIO/base-address handling, linker or image placement, framebuffer, network,
  and other Pi 4-enablement material that could matter later
- update the knowledge base with concrete findings and exact source paths
- keep the step documentation-oriented; do not start broad code changes here

Out of scope:

- new Phoenix implementation code unless a tiny unblocker is discovered and
  separately scoped afterward
- broad subsystem design changes based only on the external reference
- Pi 5 or RP1 work

## Expected Repositories

- `phoenix-rtos-kernel`
- coordination repo
- local clone or checked-out copy of the external reference

## Expected Files Or Subsystems

- external `rpi4-osdev` boot and architecture files
- updated project knowledge-base documents with source references
- manifests and tracking updates for this research step

## Acceptance Criteria

- the external reference is inspected deeply enough to extract actionable Pi 4
  bring-up details
- important findings are preserved in the knowledge base with exact repo paths
  or URLs
- the findings narrow or sharpen at least one upcoming Pi 4 boot-first step

## Validation Plan

- Review:
  inspect the external repository structure and capture only findings that are
  directly useful for the Phoenix Pi 4 port
- Build:
  not applicable unless the external reference includes a useful reproducible
  QEMU or image-generation command worth recording
- Emulator:
  not applicable
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-rpi4b-timer-countdown-readback.md`

## Notes

- Risks:
  do not turn the external reference into a cargo-cult porting plan; capture
  only concrete Pi 4 bring-up facts, design patterns, and later-subsystem clues
- Dependencies:
  completed `STEP-0201` timer-countdown readback
- Source reminder:
  official Raspberry Pi kernel DTS files on `rpi-6.19.y` and `rpi-7.0.y` are currently identical for Pi 4 and keep `memory@0` bootloader-filled plus `stdout-path` on `serial1` (aux UART); Raspberry Pi documentation also confirms that firmware applies overlays and `dtparam`s before handing the merged DTB to the OS; this step specifically targets the root memory-node cell layout, not UART alias handling
- Architecture reminder:
  Raspberry Pi 4 Model B is based on BCM2711 with a quad-core Cortex-A72 CPU; treat `aarch64a53-generic-rpi4b` only as a temporary diagnostic lane and keep new target work centered on `aarch64a72-generic-rpi4b`
- User-visible control point before next step:
  after this research step lands, the next bounded move should either be one
  GIC PPI-state diagnostic experiment or one Pi 4 boot-path adjustment
  justified directly by the external findings
