# Current Step

## Metadata

- Step ID: `STEP-0319`
- Title: Scope the smallest outbound-window and root-bridge shaping step
- Status: `in_progress`
- Date: `2026-03-21`
- Milestone / phase: `Phase 1`

## Objective

- define the smallest BCM2711 outbound-window and root-bridge shaping slice
  that should follow the new link-state gating step without widening into xHCI
  or claiming downstream enumeration

## Scope

In scope:

- reviewing the remaining gap between the new BCM2711 link-state gating step
  and meaningful downstream enumeration
- selecting the smallest next slice that can follow without widening into xHCI
  or downstream enumeration
- preserving the current HDMI text-console baseline and the deferred SD export

Out of scope:

- SD-image export or checksum refresh
- manual hardware execution
- broad framebuffer-console redesign
- changes to the existing `usbkbd` or `pl011-tty` logic
- full BCM2711 PCIe register bring-up in the same step
- xHCI, USB enumeration, or keyboard end-to-end validation

## Expected Repositories

- coordination repo

## Expected Files Or Subsystems

- `docs/status.md`
- `docs/source-artifacts.md`
- `tracking/current-step.md`
- `tracking/step-history.md`
- `manifests/`

## Acceptance Criteria

- the next outbound-window / root-bridge step is explicitly bounded and
  justified against the current Phoenix and Circle references
- the next step does not widen into xHCI or claim live downstream enumeration
- the tracking docs clearly state the exact remaining gap after the new
  link-state gating step

## Validation Plan

- source inspection against the current Phoenix PCIe stack plus the existing
  Circle reference for BCM2711 outbound-window setup and root-bridge shaping

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-21-bcm2711-link-state-gating.md`

## Notes

- Risks:
  do not jump straight into xHCI or overclaim that config-space access alone
  or sampled link-state alone implies meaningful downstream enumeration
- Dependencies:
  completed `STEP-0318` BCM2711 link-state gating
- User-visible control point before next step:
  after this step lands, the repo should clearly show the exact smallest
  outbound-window / root-bridge move and why it comes before xHCI

Current scope finding:

- the BCM2711 backend now supplies config-space access, early host-bridge prep,
  and link-state gating for downstream accesses
- the remaining gap is now narrower:
  outbound-window setup and root-bridge shaping before downstream enumeration
  can be treated as meaningful
- Pi 4 `raspi4b` QEMU is still not expected to validate that real PCIe step
