# Current Step

## Metadata

- Step ID: `STEP-0291`
- Title: Assemble and export the refreshed Pi 4 SD image
- Status: `planned`
- Date: `2026-03-21`
- Milestone / phase: `Phase 1`

## Objective

- refresh the host-visible Pi 4 SD image so the first board trial uses the
  latest HDMI firmware refinement

## Scope

In scope:

- rerun `scripts/assemble-rpi4b-sdimg.sh`
- rerun `scripts/export-rpi4b-sdimg.sh`
- record the new host artifact checksum
- update docs/status if the exported artifact path remains the same

Out of scope:

- flashing the image
- real hardware execution
- further image-layout changes

## Expected Repositories

- coordination repo

## Expected Files Or Subsystems

- `scripts/assemble-rpi4b-sdimg.sh`
- `scripts/export-rpi4b-sdimg.sh`
- `artifacts/rpi4b/rpi4b-sd.img`
- `docs/manual-operator-instructions.md`
- `docs/status.md`
- `manifests/`
- `tracking/current-step.md`
- `tracking/step-history.md`

## Acceptance Criteria

- the VM-local Pi 4 SD image is rebuilt
- the host-visible `artifacts/rpi4b/rpi4b-sd.img` is refreshed
- the refreshed image checksum is recorded
- the docs clearly point the operator to the refreshed image path

## Validation Plan

- Build:
  use the already refreshed VM-local Pi 4 boot artifacts
- Emulator:
  not applicable
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-21-aarch64-rpi4b-hdmi-firmware-refinement.md`

## Notes

- Risks:
  avoid mixing export refresh with board execution
- Dependencies:
  completed `STEP-0290` corrected image-refresh scoping
- User-visible control point before next step:
  after this step lands, the next bounded move can start the manual Pi 4 board
  trial or scope one more tiny pre-hardware refinement
