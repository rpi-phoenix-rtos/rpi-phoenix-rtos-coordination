# Tracking

This directory contains the execution-control artifacts for the implementation.

Use these files to keep the work step-by-step and visible:

- [current-step.md](/Users/witoldbolt/phoenix-rpi/tracking/current-step.md)
  the only active step
- [step-history.md](/Users/witoldbolt/phoenix-rpi/tracking/step-history.md)
  the ledger of completed, blocked, and abandoned steps
- [step-template.md](/Users/witoldbolt/phoenix-rpi/tracking/step-template.md)
  the template for defining a new step

Rules:

- there must be at most one active step
- no implementation code should start until `current-step.md` is ready
- every completed step should appear in `step-history.md`
- validated code states should also be reflected in `manifests/`
