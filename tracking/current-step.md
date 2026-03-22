# Current Step

## Metadata

- Step ID: `STEP-0361`
- Title: Scope the smallest xHCI event-ring allocation step
- Status: `in_progress`
- Date: `2026-03-22`
- Milestone / phase: `Phase 1`

## Objective

- define the next bounded xHCI step after the run-state self-test, keeping the
  controller still pre-register-programmed event delivery and pre-enumeration

## Scope

In scope:

- choosing the smallest event-ring preparation slice after the run-state step
- checking the current `xhci.c` state against the already extracted runtime
  register and capability data
- deciding whether the next narrow move is event-ring memory allocation,
  register programming, or another smaller prerequisite
- documenting the acceptance criteria for that move

Out of scope:

- implementing xHCI code in this step
- root-hub, doorbell, or enumeration logic
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

- the next xHCI move is narrowed to one specific event-ring preparation slice
- the scoped step stays pre-interrupt-enable, pre-doorbell, and
  pre-enumeration
- the choice is grounded in the current `xhci.c` state and extracted runtime
  register facts

## Validation Plan

- inspect the current `xhci.c` implementation and extracted xHCI runtime facts
- cross-check the next seam against the existing project notes and Circle
  register definitions already in the knowledge base

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-22-xhci-run-state.md`

## Notes

- Risks:
  avoid jumping straight into interrupts or event delivery before the required
  event-ring memory and runtime-register prerequisites are isolated
- Dependencies:
  completed `STEP-0360` xHCI run-state self-test
- User-visible control point before next step:
  the next implementation step should still be a narrow preparatory xHCI slice
  rather than a broad USB-host enablement jump
