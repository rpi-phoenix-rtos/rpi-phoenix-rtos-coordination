# Manifest: Scope GIC Timer Registration / Dispatch Visibility

- Date: `2026-03-20`
- Step: `STEP-0145`
- Status: `completed`

## Goal

- define the smallest next diagnostic step that can distinguish “timer handler never registered” from “timer IRQ never dispatched” for the selected common AArch64 timer IRQ

## Decision

The next implementation step is bounded to:

- `sources/phoenix-rtos-kernel/hal/aarch64/interrupts_gicv2.c`
- add tightly filtered, one-time markers for:
  - timer-handler registration
  - first dispatch of the selected timer IRQ

## Why This Step

- `STEP-0144` proved that the common timer frontend selects `physical-nonsecure irq 30` and that wakeup arming reaches `gtimer_timer.c`
- `STEP-0142` already proved that `threads_timeintr()` never runs
- `interrupts_gicv2.c` is therefore the narrowest shared place that can distinguish:
  - handler registration failure
  - handler registration success with no dispatch
  - dispatch occurring before control reaches `threads_timeintr()`

## Explicitly Deferred

- changing timer-source policy
- changing GIC routing policy
- changing scheduler wakeup policy
- real-hardware-only debugging

## Acceptance Criteria

- the next implementation patch stays local to `hal/aarch64/interrupts_gicv2.c`
- the marker plan exposes timer-handler registration and first timer-IRQ dispatch
- the scope preserves the generic `virt` and Pi 4 DTB-backed QEMU lanes as the current validation targets

## Selected Next Step

- implement filtered GIC timer registration / dispatch markers and rerun both QEMU lanes
