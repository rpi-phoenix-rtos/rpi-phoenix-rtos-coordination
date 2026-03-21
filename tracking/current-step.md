# Current Step

## Metadata

- Step ID: `STEP-0289`
- Title: Refresh the host-visible Pi 4 SD image after the HDMI firmware refinement
- Status: `planned`
- Date: `2026-03-21`
- Milestone / phase: `Phase 1`

## Objective

- update the host-visible Pi 4 SD image artifact so the first real board trial
  uses the newly refined HDMI firmware config

## Scope

In scope:

- rerun the existing Pi 4 SD-image export helper
- verify the host-visible image path
- record the refreshed artifact checksum
- update the operator-facing docs and status if needed

Out of scope:

- changing image layout
- flashing the image
- real hardware execution

## Expected Repositories

- coordination repo

## Expected Files Or Subsystems

- `scripts/export-rpi4b-sdimg.sh`
- `artifacts/rpi4b/`
- `docs/manual-operator-instructions.md`
- `docs/status.md`
- `manifests/`
- `tracking/current-step.md`
- `tracking/step-history.md`

## Acceptance Criteria

- the host-visible `artifacts/rpi4b/rpi4b-sd.img` is refreshed from the latest
  VM-local image
- the refreshed image checksum is recorded
- the docs make it clear that the exported image now includes the HDMI firmware
  refinement

## Validation Plan

- Build:
  use the already rebuilt Pi 4 VM-local image
- Emulator:
  not applicable
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-21-aarch64-rpi4b-hdmi-firmware-refinement.md`

## Notes

- Risks:
  avoid folding artifact export into flashing or hardware testing
- Dependencies:
  completed `STEP-0288` refreshed-image handoff scoping
- User-visible control point before next step:
  after this step lands, the next bounded move can start the first manual Pi 4
  board trial or scope one more small pre-hardware refinement
