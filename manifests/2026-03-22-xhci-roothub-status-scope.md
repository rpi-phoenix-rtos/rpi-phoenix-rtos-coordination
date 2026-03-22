# STEP-0373

## Title

Scope the smallest xHCI roothub status-delivery step

## Date

2026-03-22

## Outcome

The next bounded xHCI move is now fixed:

- add a temporary Pi 4 xHCI roothub status-delivery path by polling
  `xhci_getHubStatus()`
- complete the pending root-hub interrupt transfer when status bits appear
- keep the step strictly at roothub status delivery

## Why This Step

The current Pi 4 xHCI path can now survive initialization, but it still has no
way to surface plug events:

- the discovery stub still reports `irq = 0`
- there is no xHCI interrupt thread yet
- Phoenix hub handling expects the pending root-hub interrupt transfer to be
  completed when port-change status appears

So the smallest correct bridge is a temporary polling loop, not premature child
enumeration or live image staging.

## Next Step

- implement the smallest xHCI roothub status-delivery step
