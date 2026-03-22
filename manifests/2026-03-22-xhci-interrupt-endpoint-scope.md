# STEP-0387

## Title

Scope the smallest xHCI interrupt-IN endpoint step

## Date

2026-03-22

## Outcome

The next bounded xHCI move is now fixed:

- add the first non-EP0 endpoint ownership/configuration slice for the current
  direct-root-port child
- start with a single interrupt-IN endpoint only
- include:
  - endpoint ID derivation from the current `usb_pipe_t`
  - one transfer-ring allocation
  - one endpoint-context population step
  - one bounded `Configure Endpoint` command
- keep it pre-transfer-submission and pre-report-delivery

## Why This Step

After the new bounded control-transfer support, the current `usbkbd` path can
advance through:

- `usb_setConfiguration()`
- `CLASS_REQ_SET_PROTOCOL`
- `CLASS_REQ_SET_IDLE`

But it still cannot receive reports because:

- `usb_open(... interrupt, in)` is only descriptor/pipe bookkeeping
- the HCD first becomes responsible when the first interrupt URB is submitted
- before that transfer can succeed, xHCI still needs endpoint context and ring
  ownership for the keyboard interrupt-IN endpoint

So the smallest useful next seam is endpoint ownership plus `Configure
Endpoint`, not yet transfer submission or asynchronous completion delivery.

## Next Step

- implement the bounded xHCI interrupt-IN endpoint ownership/configuration step
