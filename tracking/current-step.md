# Current Step

## Metadata

- Step ID: `STEP-0314`
- Title: Implement the compile-only BCM2711 indexed config-space backend
- Status: `in_progress`
- Date: `2026-03-21`
- Milestone / phase: `Phase 1`

## Objective

- implement the smallest BCM2711-specific PCIe backend behind the new
  server-local config-space abstraction without widening into link training,
  DMA windows, MSI, or xHCI

## Scope

In scope:

- adding a BCM2711-specific indexed config-space backend behind `pcie_cfgio_t`
- selecting that backend through Pi 4 project/build settings
- preserving the existing Xilinx ECAM compile path
- preserving the current HDMI text-console baseline and the deferred SD export

Out of scope:

- SD-image export or checksum refresh
- manual hardware execution
- broad framebuffer-console redesign
- changes to the existing `usbkbd` or `pl011-tty` logic
- full BCM2711 PCIe register bring-up in the same step
- xHCI, USB enumeration, or keyboard end-to-end validation

## Expected Repositories

- `phoenix-rtos-devices`
- `phoenix-rtos-project`
- coordination repo

## Expected Files Or Subsystems

- `phoenix-rtos-devices/pcie/server/`
- `phoenix-rtos-devices/_targets/`
- `phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/`
- `docs/status.md`
- `docs/source-artifacts.md`
- `tracking/current-step.md`
- `tracking/step-history.md`
- `manifests/`

## Acceptance Criteria

- a BCM2711-specific indexed config-space backend exists behind `pcie_cfgio_t`
- the Pi 4 project exports the minimal build selection needed to compile that
  backend
- the change preserves the current Xilinx compile lane and the current Pi 4
  HDMI-text baseline

## Validation Plan

- source inspection against the current Phoenix PCIe stack plus the existing
  Circle reference for BCM2711 config-space access
- compile the touched Pi 4 PCIe server path plus the strongest preserved
  Xilinx compile lane available before claiming progress

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-21-bcm2711-pcie-backend-scope.md`

## Notes

- Risks:
  do not jump straight into link training, DMA windows, MSI, or xHCI
- Dependencies:
  completed `STEP-0313` BCM2711 backend scoping
- User-visible control point before next step:
  after this step lands, the repo should clearly show the first BCM2711
  backend hook and the exact remaining gap to real host-bridge bring-up

Current scope finding:

- Circle indicates that the smallest real BCM2711 backend behavior is:
  root-complex slot-0 direct-register access on bus 0 plus indexed
  `PCIE_EXT_CFG_INDEX` / `PCIE_EXT_CFG_DATA` access for downstream buses
- Pi 4 `raspi4b` QEMU is still not expected to validate the real PCIe step, so
  this implementation should stay compile-driven and not claim live link-up
