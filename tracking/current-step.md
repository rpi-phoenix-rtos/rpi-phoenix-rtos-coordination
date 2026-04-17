# Current Step

## Metadata

- Step ID: `STEP-0496`
- Title: `Run the next Pi 4 retry on the UART-continuity image`
- Status: `ready`
- Date: `2026-04-17`
- Milestone / phase: `Phase 1`

## Objective

- run the next real-board retry on the refreshed image
- prove whether execution continues from the custom armstub into the reloc
  trampoline without depending on LED timing
- keep the retry on one narrow question:
  does the `firmware -> armstub -> trampoline` UART chain now stay readable?

## Scope

In scope:

- one real-device retry on the refreshed image
- firmware-profile UART capture
- fallback postswitch capture only if needed
- HDMI observation
- narrow classification from `AS0` and `TR0..TR3`

Out of scope:

- new broad LED probe maps
- deeper kernel/userspace diagnosis before verifying UART continuity
- unrelated DTB/runtime refactors

## Acceptance Criteria

- the refreshed image is actually tried on real hardware
- the retry captures at least one raw UART log
- the retry can distinguish at least:
  - no post-firmware Phoenix UART
  - armstub reached, trampoline not reached
  - trampoline reached

## Validation Plan

- on the next board retry:
  - capture UART with `--profile firmware`
  - look for `AS0`
  - look for `TR0..TR3`
  - use `--profile postswitch` only if the `firmware` run never resumes after
    the firmware baud switch

## Rollback / Baseline

- current image to test:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
  (SHA-256: `8d4770cdf96a6af16fb1a1c85c75cdd267aff839caf8998f523dd2dac4a9ee15`)
- previous late-seam LED/UART-reset image:
  `405396dbd5328393223787288d832cea98ca28c417eacc8b1cbea72d316760a9`

## Notes

- the just-closed preparation step is recorded in:
  `manifests/2026-04-17-pi4-uart-routing-and-continuity-fix.md`
- official Raspberry Pi docs say Pi 4 should use `disable-bt` or `miniuart-bt`
  to make PL011 `UART0` primary on `GPIO14/15`
- the chosen current path is `dtoverlay=miniuart-bt` because it preserves a
  Bluetooth lane while still making PL011 primary
- official docs also require a fixed core clock with `miniuart-bt`, which is
  why the image keeps:
  - `force_turbo=1`
  - `core_freq=250`
- the real firmware still logs:
  - `uart: Set PL011 baud rate to 103448.300000 Hz`
- the current fix does not suppress that firmware behavior; it reinitializes
  PL011 back to `115200` immediately in:
  - `phoenix-armstub8-rpi4.S`
  - `phoenix-kernel8-reloc.S`
- the current first Phoenix-owned breadcrumbs are:
  - `AS0`
  - `TR0`
  - `TR1`
  - `TR2`
  - `TR3`
