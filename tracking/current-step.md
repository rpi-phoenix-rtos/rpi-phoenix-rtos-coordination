# Current Step

## Metadata

- Step ID: `STEP-0333`
- Title: Scope the smallest first runtime-safe xHCI initialization slice
- Status: `in_progress`
- Date: `2026-03-21`
- Milestone / phase: `Phase 1`

## Objective

- define the smallest runtime-safe xHCI initialization slice that should follow
  the new A72 USB build glue without widening into staged keyboard support

## Scope

In scope:

- reviewing the remaining gap between the new A72 USB build outputs and the
  first runtime-meaningful xHCI behavior
- selecting the smallest xHCI init slice that can move beyond `-ENOSYS`
  without staging `/sbin/usb` into the live Pi 4 image yet
- preserving the current HDMI text-console baseline and the deferred SD export

Out of scope:

- SD-image export or checksum refresh
- manual hardware execution
- broad framebuffer-console redesign
- changes to the existing `usbkbd` or `pl011-tty` logic
- broad BCM2711 PCIe host-bridge redesign
- staged runtime USB host support
- USB enumeration or keyboard end-to-end validation

## Expected Repositories

- coordination repo
- `phoenix-rtos-devices`

## Expected Files Or Subsystems

- `sources/phoenix-rtos-devices/usb/xhci/`
- `sources/phoenix-rtos-usb/usb/`
- `docs/status.md`
- `docs/source-artifacts.md`
- `tracking/current-step.md`
- `tracking/step-history.md`
- `manifests/`

## Acceptance Criteria

- the first runtime-safe xHCI init step is explicitly bounded and justified
  against the current Phoenix USB HCD model plus the new A72 build outputs
- the next step does not stage an unready USB runtime path on the live Pi 4
  image
- the tracking docs clearly state the exact remaining gap after the new A72 USB
  build-glue step

## Validation Plan

- source inspection against the current xHCI skeleton, A72 USB build outputs,
  and the Pi 4 fast-path references

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-21-aarch64a72-usb-build-glue.md`

## Notes

- Risks:
  do not stage a USB host binary that would fail the current Pi 4 boot path
  just because the first xHCI code compiles
- Dependencies:
  completed `STEP-0332` A72 USB build glue
- User-visible control point before next step:
  after this step lands, the repo should clearly show the exact smallest
  xHCI skeleton move and why it comes before staged runtime USB support

Current scope finding:

- the Pi 4 board config now carries the first VL805 fast-path assumptions taken
  from Circle:
  `bus 1 / slot 0 / func 0`, class code `0x0c0330`, and MMIO through the
  outbound PCIe window
- the current Phoenix USB stack already provides the generic HCD, hub,
  enumeration, and keyboard-driver layers
- the first compile-valid xHCI skeleton and discovery stub now exist, and the
  generic USB host binary also now compiles cleanly on the Pi 4 A72 lane after
  the `uintptr_t` fix in `phoenix-rtos-usb`
- the normal A72 build flow now also produces `/sbin/usb` and `/sbin/usbkbd`
  without staging them into the live Pi 4 image
- the remaining gap is now narrower:
  the xHCI runtime path still stops at `-ENOSYS`, so the next useful move is a
  first runtime-safe initialization slice before staging anything
- Pi 4 `raspi4b` QEMU is still not expected to validate that real xHCI path
