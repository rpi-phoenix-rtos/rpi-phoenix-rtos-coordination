# Current Step

## Metadata

- Step ID: `STEP-0368`
- Title: Implement the smallest polled xHCI command-submission step
- Status: `in_progress`
- Date: `2026-03-22`
- Milestone / phase: `Phase 1`

## Objective

- prove the first end-to-end controller command path through the command ring,
  doorbell `0`, and event ring using polling only

## Scope

In scope:

- enqueueing a single command no-op TRB
- ringing doorbell `0`
- polling the event ring for a command-completion event
- validating that the completion corresponds to the submitted command

Out of scope:

- interrupt-driven completions
- root-hub logic or enumeration
- USB keyboard device bring-up
- SD-image export or checksum refresh
- manual hardware execution
- unrelated shell, console, or PCIe changes

## Expected Repositories

- coordination repo
- `phoenix-rtos-kernel`

## Expected Files Or Subsystems

- `sources/phoenix-rtos-devices/usb/xhci/xhci.c`
- `docs/status.md`
- `docs/source-artifacts.md`
- `tracking/current-step.md`
- `tracking/step-history.md`
- `manifests/`

## Acceptance Criteria

- the xHCI path can complete one bounded command through:
  - command ring
  - doorbell `0`
  - event ring
- the step stays polled and pre-enumeration
- the full `aarch64a72-generic-rpi4b` build still succeeds

## Validation Plan

- fresh `aarch64a72-generic-rpi4b` build from the copied VM-local buildroot in
  `phoenix-dev`
- preserve the staged Pi 4 image composition until this path is further proven

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-22-xhci-polled-command-scope.md`

## Notes

- Risks:
  avoid widening directly into full enumeration; keep the first command path to
  one internal no-op command with polled completion
- Dependencies:
  completed `STEP-0367` xHCI polled-command scope
- User-visible control point before next step:
  the next implementation step should answer a concrete question:
  whether the current Pi 4 xHCI path can complete one command at all
