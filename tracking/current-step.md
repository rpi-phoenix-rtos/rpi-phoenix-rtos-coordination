# Current Step

## Metadata

- Step ID: `STEP-0356`
- Title: Implement the smallest bounded `psh_ttyopen()` retry-policy refinement for the Pi 4 shell startup race
- Status: `in_progress`
- Date: `2026-03-22`
- Milestone / phase: `Phase 1`

## Objective

- restore the Pi 4 `raspi4b` shell smoke after the post-xHCI validation pass
  with the smallest shell-side console-open timing fix

## Scope

In scope:

- a small retry-policy refinement in `psh` startup around `psh_ttyopen()`
- preserving the current console architecture and `/dev/console` usage
- validating through:
  - generic shell smoke
  - Pi 4 shell smoke
  - Pi 4 HDMI smoke if needed

Out of scope:

- SD-image export or checksum refresh
- manual hardware execution
- xHCI, PCIe, or USB-host runtime behavior changes
- `pl011-tty` logic changes
- kernel namespace redesign
- removing historical debug probes in unrelated files

## Expected Repositories

- coordination repo
- `phoenix-rtos-utils`

## Expected Files Or Subsystems

- `sources/phoenix-rtos-utils/psh/`
- `docs/status.md`
- `docs/source-artifacts.md`
- `docs/testing-automation.md`
- `tracking/current-step.md`
- `tracking/step-history.md`
- `manifests/`

## Acceptance Criteria

- the Pi 4 shell smoke reaches `(psh)%` and completes the `help` round-trip
  again
- generic shell smoke stays green
- the fix remains small and localized to shell startup policy

## Validation Plan

- fresh `aarch64a72-generic-rpi4b` build from the copied VM-local buildroot in
  `phoenix-dev`
- `./scripts/qemu-shell-smoke.sh generic`
- `./scripts/qemu-shell-smoke.sh rpi4b`
- `/bin/bash /Users/witoldbolt/phoenix-rpi/scripts/qemu-rpi4b-hdmi-smoke.sh`

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-22-pi4-console-open-race-scope.md`

## Notes

- Risks:
  do not widen the fix into namespace, kernel, or console-driver redesign if a
  bounded retry-policy change is enough
- Dependencies:
  completed `STEP-0355` Pi 4 console-open race scope
- User-visible control point before next step:
  after this step lands, the Pi 4 `raspi4b` shell smoke should be green again
