# Current Step

## Metadata

- Step ID: `STEP-0276`
- Title: Scope the smallest full Pi 4 SD-card image step
- Status: `planned`
- Date: `2026-03-21`
- Milestone / phase: `Phase 1`

## Objective

- choose the smallest next artifact step that makes the current Pi 4 boot image
  straightforward to flash onto a real microSD card

## Scope

In scope:

- decide whether to keep using the exported FAT image directly for hardware
  flashing or to wrap it in a full-disk image
- keep the step artifact-only and no-hardware
- keep the decision grounded in the current user hardware constraints:
  microSD present, no USB-UART adapter, HDMI plus Ethernet available

Out of scope:

- implementing the full-disk image helper itself
- changing Phoenix source code
- real hardware execution

## Expected Repositories

- coordination repo

## Expected Files Or Subsystems

- host-visible `artifacts/rpi4b/rpi4b-bootfs.img`
- existing FAT image assembly helpers
- current manual hardware instructions
- `docs/status.md`
- `manifests/`
- `tracking/current-step.md`
- `tracking/step-history.md`

## Acceptance Criteria

- the next artifact step is selected explicitly
- the selection reflects the fact that the first operator has a Pi 4 board,
  microSD card, HDMI display, Ethernet, keyboard, and mouse, but no USB-UART
  adapter
- no Phoenix upstream repo changes are introduced

## Validation Plan

- Inspect the current artifact shape and the manual hardware workflow
- Confirm whether the next operator-facing gap is SD-card usability rather than
  another host-copy detail
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-21-aarch64-rpi4b-fat-image-export-helper.md`

## Notes

- Risks:
  avoid widening into persistent-storage or multi-partition runtime work
- Dependencies:
  completed `STEP-0275` FAT-image export helper
- User-visible control point before next step:
  after the scope decision, the next bounded code step can add one SD-card
  image helper and keep the current boot payload unchanged
