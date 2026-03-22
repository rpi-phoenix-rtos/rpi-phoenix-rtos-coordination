# STEP-0363

## Title

Scope the smallest xHCI runtime-register programming step for the new event ring

## Date

2026-03-22

## Outcome

The next bounded xHCI move is now fixed:

- program runtime interrupter `0` registers for the already allocated event-ring
  state:
  - `ERSTSZ`
  - `ERSTBA`
  - `ERDP`
- read those registers back and sanity-check the programmed values
- keep the step strictly:
  - pre-interrupt-enable
  - pre-doorbell
  - pre-root-hub
  - pre-enumeration

## Why This Step

After `STEP-0362`, the first event-delivery structures already exist in memory.
The next smallest useful move is no longer more allocation. It is binding those
structures into the controller-visible runtime register set for interrupter `0`.

That keeps the following interrupt-enable or event-consumption work narrow and
local.

## Next Step

- implement the smallest xHCI runtime-register programming step for the new
  event ring
