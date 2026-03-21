# 2026-03-21: scope the smallest post-Circle Pi 4 HDMI refinement

## Scope

- Step: `STEP-0296`
- Goal: select the smallest next Pi 4 refinement after the Circle review

## Repositories Touched

- coordination repo

## Decision

The selected next step is:

- implement a tiny staged HDMI progress indicator in `plo`

This means:

- keep the current firmware mailbox plus framebuffer allocation path
- keep the current no-UART focus
- replace the single undifferentiated rectangle with a few stable visual
  progress markers

## Why This Step

The Circle review produced two decisive conclusions:

1. Circle strongly validates the current firmware mailbox framebuffer approach
   for early Pi 4 HDMI life signs.
2. Circle also proves that Pi 4 USB keyboard support is not a cheap next step,
   because it depends on PCIe plus xHCI plus HID keyboard support.

For the current real-board lab:

- HDMI is available
- UART is not
- USB keyboard is available physically but not yet technically reachable

So the highest-value next move is to improve what the HDMI path communicates,
not to widen into USB.

## Chosen Shape

The next code step should:

- stay entirely in the early `plo` framebuffer path
- avoid introducing a real text console or runtime graphics stack
- render a few deterministic stage markers instead of one plain block
- light additional markers at later points such as:
  - framebuffer ready
  - late `hal_init` reached
  - kernel jump started

## Result

- `STEP-0296` is complete
- the next active step is to implement the staged `plo` HDMI progress
  indicator and validate it under `raspi4b` QEMU

