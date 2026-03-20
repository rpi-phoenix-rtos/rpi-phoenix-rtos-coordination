# Manifest: Pi 4 Timer Countdown Readback Scope

- Date: `2026-03-20`
- Step: `STEP-0200`
- Status: `completed`

## Goal

- select the smallest timer-side follow-up after the Pi 4 pending-state probe
  showed that the chosen timer IRQ never becomes pending

## Evidence Reviewed

Current generic `virt` lane evidence:

- `gtimer: arm 1000 us ctl 0x1 tval 59617`
- `gtimer: pending 1`
- `gic: timer dispatch`
- `threads: timer irq`

Current Pi 4 A72 patched-lane evidence:

- `gtimer: source physical-nonsecure irq 30`
- `gtimer: arm 1000 us ctl 0x1 tval ...`
- `gtimer: pending 0`
- no `gic: timer dispatch`
- no `threads: timer irq`

Current unanswered question:

- whether the Pi 4 timer actually counts down after the first arm or remains
  inert before ever reaching the GIC pending state

## Selected Next Experiment

- add one bounded first-arm countdown readback after the existing 2 ms pending
  probe window and log the post-wait timer control plus timer value

## Why This Is The Right Next Step

- it stays strictly on the timer side of the current boundary
- it does not widen into GIC, scheduler, or VM rework
- it distinguishes two materially different failure classes:
  - the timer is not counting down after the arm
  - the timer is counting down but still never asserts an interrupt
- it reuses the same bounded first-arm window already used by the pending probe

## Selected Implementation Shape

- keep the change diagnostic-only
- emit at most one additional first-arm countdown line
- reuse the existing first-arm timer trace path in
  `sources/phoenix-rtos-kernel/hal/aarch64/gtimer_timer.c`

## Selected Next Step

- implement the bounded first-arm countdown readback experiment and validate it
  on the generic `virt` guardrail lane and the Pi 4 A72 patched lane
