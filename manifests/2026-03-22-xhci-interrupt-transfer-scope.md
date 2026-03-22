# STEP-0389

## Title

Scope the smallest xHCI interrupt-IN transfer step

## Date

2026-03-22

## Outcome

The next bounded xHCI move is now fixed:

- support exactly one outstanding interrupt-IN transfer on the current
  configured child endpoint
- use a single normal TRB on the already-allocated interrupt ring
- keep the current no-IRQ design and deliver completion through the existing
  xHCI status thread by polling the event ring
- keep the step below generic multi-endpoint scheduling, cancellation, and live
  Pi 4 image staging

## Why This Step

After the new interrupt-endpoint ownership/configuration step:

- `usbkbd` can reach the point where it allocates interrupt URBs
- but a configured endpoint still cannot deliver any report, because
  `xhci_transferEnqueue()` still stops before real interrupt transfer
  submission
- the current Pi 4 discovery stub still reports `irq = 0`, so the first useful
  completion path is still polling, not interrupt enablement

So the smallest useful next seam is one outstanding interrupt-IN transfer plus
completion delivery on the existing no-IRQ thread.

## Next Step

- implement the bounded xHCI interrupt-IN transfer and no-IRQ completion step
