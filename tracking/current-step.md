# Current Step

## Metadata

- Step ID: `STEP-0297`
- Title: Implement the staged Pi 4 `plo` HDMI progress indicator
- Status: `planned`
- Date: `2026-03-21`
- Milestone / phase: `Phase 1`

## Objective

- improve early Pi 4 no-UART observability by turning the current `plo` HDMI
  marker into a small staged progress indicator

## Scope

In scope:

- update the early Pi 4 `plo` framebuffer drawing
- add at most a tiny stage-update hook for late `hal_init` or kernel jump
- keep the change strictly inside early loader HDMI observability

Out of scope:

- runtime graphics support
- text-console implementation
- PCIe, xHCI, or USB keyboard work
- changes to the kernel runtime display path

## Expected Repositories

- `plo`
- coordination repo

## Expected Files Or Subsystems

- `sources/plo/hal/aarch64/generic/video.c`
- `sources/plo/hal/aarch64/generic/hal.c`
- `scripts/qemu-rpi4b-hdmi-smoke.sh`
- `docs/status.md`
- `manifests/`
- `tracking/current-step.md`
- `tracking/step-history.md`

## Acceptance Criteria

- the Pi 4 `plo` HDMI path shows more than one boot state
- the result remains a tiny early-loader visual indicator, not a text console
- the updated indicator is validated under `raspi4b` QEMU

## Validation Plan

- Build:
  rebuild the Pi 4 project lane
- Emulator:
  run the Pi 4 HDMI smoke or a similarly narrow `raspi4b` QEMU check
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-21-aarch64-rpi4b-post-circle-hdmi-scope.md`

## Notes

- Risks:
  avoid accidentally growing this into a framebuffer console design
- Dependencies:
  completed `STEP-0296` post-Circle HDMI step selection
- User-visible control point before next step:
  after this step lands, the next bounded move should either prepare the first
  real-board trial or make one more tiny HDMI-specific refinement if the new
  staged indicator still leaves an obvious gap
