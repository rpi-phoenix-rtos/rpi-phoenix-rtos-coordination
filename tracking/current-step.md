# Current Step

## Metadata

- Step ID: `STEP-0488`
- Title: Await the next Pi 4 board retry on the reproducible cleanup image
- Status: `ready`
- Date: `2026-04-17`
- Milestone / phase: `Phase 1`

## Objective

- verify that the cleaned Pi 4 image still reaches the same late-boot /
  userspace boundary without the legacy GPIO42 diagnostics
- confirm that the normal UART lane is readable at `115200 8N1`
- classify whether the `dummyfs` / `devfs` / `pl011-tty` path is now improved,
  unchanged, or regressed on real hardware

## Scope

In scope:
- one new real-device Pi 4 retry on the cleaned reproducible image
- HDMI observation
- UART capture at `115200`
- fallback `postswitch` capture only if the firmware still overrides the baud
- classification of the resulting late-boot boundary

Out of scope:
- new Pi 4 boot logic before seeing the next real-device result
- reintroducing legacy GPIO42 stage telemetry

## Acceptance Criteria

- the cleaned image boots at least as far as the last known late-boot boundary
  on real hardware
- the UART lane is readable at `115200` or a fallback `postswitch` capture
  proves why it is not
- the next blocker is classified from real evidence instead of stale tracker
  assumptions

## Validation Plan

- flash `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- capture UART with:
  - `./scripts/capture-rpi4b-uart.sh --profile firmware --device /dev/cu.usbserial-XXXX --label pi4-firmware`
- summarize with:
  - `./scripts/summarize-rpi4b-uart-log.py /path/to/log`
- capture HDMI screenshot or photo if the screen advances
- use `--profile postswitch` only if the `firmware` log still stops at a
  firmware baud-switch line

## Rollback / Baseline

- current reproducible Pi 4 image:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
  (SHA-256: `eff8ca6193da33baeeb5af6c7fee3deefbd6a6243388b5cc708544bab2dd210e`)
- exact committed repo heads recorded in:
  `manifests/2026-04-17-pi4-legacy-led-cleanup-baseline.md`

## Notes

- the old legacy GPIO42 diagnostics are now removed from:
  - `phoenix-armstub8-rpi4.S`
  - `plo/hal/aarch64/generic/_init.S`
  - `phoenix-rtos-kernel/hal/aarch64/_init.S`
  - `dummyfs/srv.c`
  - the Pi 4 `board_config.h` toggle block
- this step is intentionally a manual hardware boundary
