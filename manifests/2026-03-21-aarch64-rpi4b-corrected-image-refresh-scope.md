# 2026-03-21: scope the corrected refreshed-image sequence

## Scope

- Step: `STEP-0290`
- Goal: correct the refreshed-image handoff sequence after the failed direct
  export attempt

## Decision

The smallest corrected sequence is:

1. rerun `scripts/assemble-rpi4b-sdimg.sh`
2. rerun `scripts/export-rpi4b-sdimg.sh`
3. record the refreshed host-visible checksum

## Why This Is Correct

- `build.sh project image` refreshes the staged boot payloads
- `scripts/assemble-rpi4b-sdimg.sh` rebuilds the full disk image wrapper around
  those payloads
- `scripts/export-rpi4b-sdimg.sh` then copies that rebuilt VM-local disk image
  to the host-visible artifact path

## Next Step

- implement `STEP-0291`: rerun the two existing helpers in sequence and record
  the new host artifact checksum
