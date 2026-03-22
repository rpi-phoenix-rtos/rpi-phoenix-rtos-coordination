# STEP-0361

## Title

Scope the smallest xHCI event-ring allocation step

## Date

2026-03-22

## Outcome

The next bounded xHCI move is now fixed:

- allocate the first event-ring segment
- allocate one first ERST block for interrupter 0
- populate one ERST entry with the allocated event-ring segment base and TRB
  count
- keep the step strictly:
  - pre-runtime-register programming
  - pre-interrupt-enable
  - pre-doorbell
  - pre-root-hub
  - pre-enumeration

## Why This Step

After `STEP-0360`, the controller path already has:

- validated controller capabilities and runtime-register layout
- allocated and programmed command-space memory
- verified a bounded halted-to-run-to-halted state transition

The next smallest useful prerequisite for real controller event delivery is not
another passive read or another run-state experiment. It is the first event-ring
memory foundation:

- an event-ring segment
- an ERST
- a valid ERST entry describing that segment

That keeps the next runtime-register-programming step simple and local.

## Next Step

- implement the smallest xHCI event-ring allocation step
