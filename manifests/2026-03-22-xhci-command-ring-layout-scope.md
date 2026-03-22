# STEP-0365

## Title

Scope the smallest xHCI command-ring layout initialization step

## Date

2026-03-22

## Outcome

The next bounded xHCI move is now fixed:

- initialize the allocated command ring as a real ring layout
- add the final link TRB that points back to the first TRB
- establish the initial command-ring cycle state in memory
- keep the step strictly:
  - pre-doorbell
  - pre-interrupt-enable
  - pre-root-hub
  - pre-enumeration

## Why This Step

After `STEP-0364`, the controller-visible register state already points at:

- command-ring memory through `CRCR`
- event-ring memory through `ERSTSZ`, `ERSTBA`, and `ERDP`

The next clean prerequisite for any future command submission is not enabling
interrupts yet. It is making the command-ring backing memory an actual ring
instead of zero-filled storage.

## Next Step

- implement the smallest xHCI command-ring layout initialization step
