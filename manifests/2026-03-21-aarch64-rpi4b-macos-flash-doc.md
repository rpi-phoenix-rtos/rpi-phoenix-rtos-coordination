# Manifest: Pi 4 macOS Flashing-Workflow Runbook

- Date: `2026-03-21`
- Step: `STEP-0281`
- Scope: document the first macOS flashing workflow and current no-UART
  expectations for the host-visible Pi 4 SD-card image

## Change

- update:
  - `docs/manual-operator-instructions.md`
  - `docs/testing-automation.md`
  - `docs/status.md`

## Validation

- review the documented artifact path:
  - `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- review the documented exported-image SHA-256:
  - `d480e6d35d91a6e9b4d56971fd8973feb45140d570c099ee4c638fa5179cb0bc`
- confirm the runbook now includes:
  - one explicit macOS flashing procedure
  - current no-UART first-boot limitations
  - the current next technical direction after artifact preparation

## Result

- the operator runbook now includes a concrete macOS `diskutil` plus `dd`
  workflow for writing `rpi4b-sd.img` to microSD
- the docs now explicitly say that the first no-UART boot attempt is an
  artifact-deployment exercise, not a strong runtime-validation milestone
- the current next bounded technical move is now explicit:
  alternate observability for a Pi 4 lab without USB-TTL serial

## Conclusion

- the project is now materially closer to the first manual Pi 4 hardware trial
- the next bounded technical path should stop focusing on boot artifact shape
  and start focusing on a visible runtime signal beyond UART
