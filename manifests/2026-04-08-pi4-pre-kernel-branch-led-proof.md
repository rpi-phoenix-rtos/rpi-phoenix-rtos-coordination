# 2026-04-08 Pi 4 Pre-Kernel-Branch LED Proof

## Scope

Implement the next smallest real-hardware LED split after the temporary
late-`plo` proof was disproved, by moving the next persistent GPIO42 state
change into the custom Pi 4 armstub just before it branches to `kernel8.img`.

## Starting Point

The latest real Pi 4 evidence before this step was:

- custom armstub entry is proven:
  the ACT LED turns on and stays on with the armstub GPIO42-high proof
- late `plo` is not yet proven:
  the temporary late-`plo` `_init.S` split still ended with both LEDs on

That made the final primary-core armstub handoff point the next highest-value
split.

## Final Changes

### `plo`

`_startc.c`

- removed the earlier `_startc()` GPIO42-low proof
- that `_startc()` probe had already served its purpose and no longer matched
  the current narrower handoff question
- upstream commit:
  - `6c8dc77`

### `phoenix-rtos-project`

`_projects/aarch64a72-generic-rpi4b/phoenix-armstub8-rpi4.S`

- added:
  - `GPIO_GPCLR1`
  - `GPIO42_CLR`
  - `gpio42_led_off`
- on the primary-core path, just before the handoff into `kernel8.img`,
  the custom armstub now drives GPIO42 low

`_projects/aarch64a72-generic-rpi4b/board_config.h`

- removed the no-longer-used `plo` GPIO42 constants that were only needed by
  the earlier `_startc()` proof

- upstream commit:
  - `8b605b0`

## Intended Board Semantics

With the new image:

- ACT LED stays on:
  the board still fails before the final primary-core armstub handoff point
- ACT LED ends off:
  the board reached the final primary-core armstub handoff point and the next
  failure is later, in the branch-to-kernel contract or earliest runtime entry

## Validation

### Build

- rebuilt `aarch64a72-generic-rpi4b` in `phoenix-dev`

### No-hardware sanity

- the direct Pi 4 QEMU serial-log sanity lane still reaches:
  - `go!`
  - `hal: jump exit el1`
  - kernel markers `A3` and `KLM`
- the Pi 4 HDMI pixel smoke was not used as the step gate here, because it
  currently reflects a loader-background expectation mismatch rather than a
  meaningful regression in the boot continuity path

### Artifact chain

- reran:
  - `scripts/assemble-rpi4b-bootfs.sh`
  - `scripts/assemble-rpi4b-bootfs-img.sh`
  - `scripts/assemble-rpi4b-sdimg.sh`
  - `scripts/export-rpi4b-sdimg.sh`
  - `scripts/verify-rpi4b-sdimg.sh`

## Current Artifact

- image:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- SHA-256:
  `f6abd64a6dcd9e254a224c73d2402c4d33e09f52eec6da36418d903e31ffddac`

## Next Step

Flash the refreshed SD image and retry the real Pi 4 board boot. Use the final
ACT LED state to choose the next smallest earliest-entry step.
