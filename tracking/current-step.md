# Current Step

## Metadata

- Step ID: `STEP-0282`
- Title: Scope the smallest alternate-observability step for a no-UART Pi 4 lab
- Status: `planned`
- Date: `2026-03-21`
- Milestone / phase: `Phase 1`

## Objective

- select the smallest next technical step that can produce a meaningful runtime
  signal on a Pi 4 board without USB-TTL serial

## Scope

In scope:

- decide whether the next bounded move should target:
  - firmware-visible HDMI behavior
  - simple framebuffer output
  - early network visibility
  - or another narrow non-UART signal path
- keep the step technical but still bounded and no-hardware if possible
- keep the decision aligned with the current user hardware:
  HDMI plus Ethernet plus USB keyboard or mouse, but no USB-TTL adapter

Out of scope:

- implementing the chosen observability path itself
- real hardware execution
- broad multi-subsystem bring-up

## Expected Repositories

- coordination repo

## Expected Files Or Subsystems

- `docs/manual-operator-instructions.md`
- `docs/testing-automation.md`
- `docs/platforms/raspberry-pi-4.md`
- current Pi 4 source and reference notes for mailbox, framebuffer, and early
  network possibilities
- `docs/status.md`
- `manifests/`
- `tracking/current-step.md`
- `tracking/step-history.md`

## Acceptance Criteria

- the next non-UART observability step is selected explicitly
- the choice is justified against the current hardware available to the
  operator
- no Phoenix upstream repo changes are introduced

## Validation Plan

- review the current no-UART lab constraints and already-available artifacts
- inspect the most promising narrow observability options in the current
  knowledge base and source references
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-21-aarch64-rpi4b-macos-flash-doc.md`

## Notes

- Risks:
  avoid widening into broad display, USB, or network subsystem implementation
- Dependencies:
  completed `STEP-0281` macOS flashing-workflow runbook step
- User-visible control point before next step:
  after the scope decision, the next bounded implementation step can target one
  visible runtime path beyond UART
