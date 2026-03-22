# STEP-0359

## Title

Scope the smallest xHCI pre-run operational step beyond command-space register programming

## Date

2026-03-22

## Outcome

The next bounded xHCI move is now fixed:

- add the first controller run-state transition helper around `RUN/STOP`
- verify the controller can leave the halted state and then return to it
- keep the step strictly:
  - pre-event-ring
  - pre-interrupt-enable
  - pre-doorbell
  - pre-root-hub
  - pre-enumeration
- still return `-ENOSYS` from `xhci_init()` after the self-test so the live
  image behavior does not change yet

## Why This Step

After `STEP-0354`, the controller already has:

- validated capability shape
- validated runtime register layout
- allocated command-space memory
- programmed `DCBAAP`, `CRCR`, and `CONFIG`

The next smallest meaningful seam is no longer another passive register read.
The controller should prove that it can:

- transition out of `HCHalted`
- avoid immediate host/system error state
- transition cleanly back to `HCHalted`

That gives a stronger foundation for the later event-ring and interrupter work
without prematurely claiming root-hub or USB-device support.

## Next Step

- implement the smallest xHCI run-state self-test beyond command-space binding
