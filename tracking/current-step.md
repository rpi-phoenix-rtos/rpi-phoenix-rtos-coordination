# Current Step

## Metadata

- Step ID: `STEP-0311`
- Title: Scope the first Pi 4 USB transport milestone after the keyboard bridge
- Status: `in_progress`
- Date: `2026-03-21`
- Milestone / phase: `Phase 1`

## Objective

- define the smallest next step that moves the project from “keyboard class
  driver plus tty bridge exist” toward a real `/dev/kbd0` on Pi 4 hardware

## Scope

In scope:

- choosing the narrowest Pi 4 USB transport seam after the class-driver and tty
  groundwork is already in place
- preferring the smallest milestone that advances toward a real keyboard device
  on Pi 4 without pretending the whole USB stack is near completion
- preserving the current HDMI text-console baseline and the deferred SD export

Out of scope:

- SD-image export or checksum refresh
- manual hardware execution
- broad framebuffer-console redesign
- changes to the existing `usbkbd` or `pl011-tty` logic unless the transport
  review proves they are immediately needed
- SD image export

## Expected Repositories

- `phoenix-rtos-devices`
- `phoenix-rtos-project`
- `phoenix-rtos-build`
- coordination repo

## Expected Files Or Subsystems

- `phoenix-rtos-devices/pcie/`
- `phoenix-rtos-devices/usb/`
- `docs/platforms/raspberry-pi-4.md`
- `docs/circle-reference-review.md`
- `docs/status.md`
- `docs/source-artifacts.md`
- `tracking/current-step.md`
- `tracking/step-history.md`
- `manifests/`

## Acceptance Criteria

- the repo records a concrete smallest-next transport milestone for Pi 4 USB
- the selected milestone clearly explains why it comes before broader xHCI or
  HID expansion
- the plan keeps the existing keyboard-class and tty-bridge work reusable

## Validation Plan

- source inspection against the current Phoenix PCIe and USB stack plus the
  existing Circle and Raspberry Pi references
- if code is implemented in the next step:
  build the strongest touched-target lane available before claiming progress

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-21-pl011-usbkbd-bridge.md`

## Notes

- Risks:
  do not skip directly to an oversized “full USB on Pi 4” step
- Dependencies:
  completed `STEP-0310` Pi 4 keyboard bridge
- User-visible control point before next step:
  after this step lands, the repo should clearly identify the smallest next
  transport milestone and why it is the best path toward real Pi 4 keyboard
  interaction

Current scope finding:

- the smallest real transport milestone is now BCM2711 PCIe root-complex
  bring-up plus ECAM enumeration, not xHCI itself
- Pi 4 `raspi4b` QEMU is not expected to validate that milestone because its
  PCIe root-port support remains incomplete
