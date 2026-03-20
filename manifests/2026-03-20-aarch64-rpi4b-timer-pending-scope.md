# Manifest: Pi 4 Timer Pending-State Scope

- Date: `2026-03-20`
- Step: `STEP-0198`
- Status: `completed`

## Goal

- select the smallest interrupt-delivery follow-up after both Pi 4 timer-source
  attempts failed to restore dispatch

## Evidence Reviewed

Current Pi 4 patched-lane evidence:

- virtual timer attempt:
  - no `gic: timer dispatch`
  - no `threads: timer irq`
- physical timer attempt:
  - `gtimer: source physical-nonsecure irq 30`
  - still no `gic: timer dispatch`
  - still no `threads: timer irq`

Current GIC visibility already available:

- group readback
- enable readback
- first dispatch marker

Current missing signal:

- whether the selected timer IRQ ever becomes pending in the GIC

## Selected Next Experiment

- add a bounded pending-state probe for the selected timer IRQ after the first
  timer arm on the Pi 4 patched lane

## Why This Is The Right Next Step

- it distinguishes two different failure classes cleanly:
  - the timer never asserts into the GIC
  - the timer asserts but still does not dispatch
- it stays inside the current interrupt-delivery boundary
- it does not require scheduler or VM changes
- it is more informative than another timer-source or broad GIC configuration
  change

## Selected Implementation Shape

- keep the probe diagnostic-only
- keep it first-arm-only to avoid flooding logs
- expose one small GIC helper for timer-IRQ pending readback

## Selected Next Step

- implement the first-arm timer-IRQ pending-state probe and validate it on the
  automated Pi 4 patched lane plus the generic guardrail lane
