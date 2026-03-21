# Current Step

## Metadata

- Step ID: `STEP-0279`
- Title: Implement the smallest host-visible Pi 4 SD-card image export helper
- Status: `planned`
- Date: `2026-03-21`
- Milestone / phase: `Phase 1`

## Objective

- implement the smallest helper that makes the new Pi 4 full disk image
  directly usable from the macOS host

## Scope

In scope:

- add one host-side export helper for the VM-local `rpi4b-sd.img`
- copy the image into a stable host-visible workspace artifact path
- validate that the exported host image matches the VM-local source
- keep the step no-hardware and operator-facing

Out of scope:

- adding SD-card flashing automation
- changing Phoenix source code
- real hardware execution

## Expected Repositories

- coordination repo

## Expected Files Or Subsystems

- VM-local `rpi4b-sd.img`
- new host-side export helper
- host-visible `artifacts/rpi4b/`
- `docs/manual-operator-instructions.md`
- `docs/status.md`
- `manifests/`
- `tracking/current-step.md`
- `tracking/step-history.md`

## Acceptance Criteria

- the helper exports the full Pi 4 disk image into a stable host-visible path
- the host-visible image matches the VM-local source by size or hash
- no Phoenix upstream repo changes are introduced

## Validation Plan

- Helper validation:
  run the new export helper
- Matching:
  confirm the host-visible disk image matches the VM-local source size or hash
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-21-aarch64-rpi4b-sdimg-export-scope.md`

## Notes

- Risks:
  avoid widening into SD-writing automation, real-device execution, or
  persistent-rootfs work
- Dependencies:
  completed `STEP-0278` SD-card image export scoping
- User-visible control point before next step:
  after this helper lands, the next bounded move can document the first manual
  SD-card flashing workflow for the current no-UART lab
