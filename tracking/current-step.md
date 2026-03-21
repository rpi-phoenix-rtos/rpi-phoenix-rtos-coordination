# Current Step

## Metadata

- Step ID: `STEP-0287`
- Title: Implement the Pi 4 firmware-stage HDMI refinement
- Status: `planned`
- Date: `2026-03-21`
- Milestone / phase: `Phase 1`

## Objective

- improve the odds and interpretability of the first real Pi 4 HDMI trial by
  making the smallest firmware-stage `config.txt` refinement selected in
  `STEP-0286`

## Scope

In scope:

- update the Pi 4 project `config.txt`
- add:
  - `hdmi_force_hotplug=1`
  - `disable_overscan=1`
- rebuild the Pi 4 artifacts
- verify the staged firmware-visible `config.txt`
- update the operator and source-reference docs

Out of scope:

- fixed explicit `hdmi_group` / `hdmi_mode`
- runtime framebuffer console support
- real hardware execution
- broad display-policy tuning

## Expected Repositories

- `phoenix-rtos-project`
- coordination repo

## Expected Files Or Subsystems

- `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/config.txt`
- `docs/manual-operator-instructions.md`
- `docs/status.md`
- `docs/source-artifacts.md`
- `manifests/`
- `tracking/current-step.md`
- `tracking/step-history.md`

## Acceptance Criteria

- the Pi 4 project `config.txt` contains the two selected HDMI refinements
- the Pi 4 image build succeeds
- the staged Pi 4 firmware `config.txt` contains those lines
- the docs record why these settings are enabled and what they are expected to
  improve on the first real board trial

## Validation Plan

- Build:
  rebuild `aarch64a72-generic-rpi4b` in `phoenix-dev`
- Emulator:
  not required for this firmware-only refinement
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-21-aarch64-rpi4b-hdmi-smoke-helper.md`

## Notes

- Risks:
  avoid expanding this into a broad compatibility matrix of HDMI firmware
  options
- Dependencies:
  completed `STEP-0286` HDMI firmware-refinement scoping
- User-visible control point before next step:
  after this step lands, the next bounded move can start using the refined
  image for the first real Pi 4 HDMI trial or scope the next smallest
  real-hardware visibility refinement
