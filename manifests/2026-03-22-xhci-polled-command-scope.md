# STEP-0367

## Title

Scope the smallest polled xHCI command-submission step

## Date

2026-03-22

## Outcome

The next bounded xHCI move is now fixed:

- add one internal helper that submits a single command-ring no-op TRB
- ring doorbell `0`
- poll the event ring for a matching command-completion event
- keep the step strictly:
  - polled
  - single-command
  - pre-root-hub
  - pre-USB-device-enumeration

## Why This Step

The current Pi 4 discovery stub in
`sources/phoenix-rtos-devices/usb/xhci/phy-aarch64a72-generic.c` still reports:

- `.irq = 0`

So the next realistic bounded step is not interrupt enablement. It is proving
that the controller can process one command through:

- command ring
- doorbell `0`
- event ring

using polling only.

## Next Step

- implement the smallest polled xHCI command-submission step
