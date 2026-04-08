# 2026-04-08 Pi 4 GIC High-Address Rebuild

## Scope

Close the next smallest real-hardware early-boot hypothesis after the custom
armstub rebuild failed to change the board symptom:

- correct the Pi 4 A72 `plo` GIC base overrides from the DT bus addresses to
  the ARM-visible high-peripheral addresses
- rebuild and revalidate the strongest relevant QEMU lanes
- export a fresh Pi 4 SD-card image for the next board retry

## Trigger

Real Pi 4 board result on the armstub rebuild remained unchanged:

- black screen
- initial ACT blinks only, then silent
- no keyboard-visible reaction

That pushed the next investigation seam back into the earliest `plo` MMIO
touches.

## Root Cause Hypothesis

The active Pi 4 board config still used:

- `PLO_GICD_BASE_ADDRESS 0x40041000`
- `PLO_GICC_BASE_ADDRESS 0x40042000`

Those are the DT bus addresses visible in
`external/raspberrypi-linux/.../bcm2711.dtsi`, but Circle's bare-metal Pi 4
code uses the ARM-visible high-peripheral aliases:

- `0xff841000`
- `0xff842000`

That mismatch is the kind of early MMIO error that can kill `plo` before any
HDMI-visible Phoenix output appears on real hardware.

## Code Change

### `phoenix-rtos-project`

File:

- `/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/board_config.h`

Commit:

- `e901b32`

Change:

- switch `PLO_GICD_BASE_ADDRESS` from `0x40041000` to `0xff841000`
- switch `PLO_GICC_BASE_ADDRESS` from `0x40042000` to `0xff842000`

## Validation

### Rebuild

Passed in `phoenix-dev`:

- `TARGET=aarch64a72-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image`

### Pi 4 QEMU

Passed:

- `./scripts/qemu-shell-smoke.sh rpi4b`
- `/bin/bash /Users/witoldbolt/phoenix-rpi/scripts/qemu-rpi4b-hdmi-smoke.sh`

### Generic QEMU Note

The generic shell-smoke wrapper did not return cleanly in the host shell during
this round, but the regenerated `/tmp/generic-shell-smoke.log` in `phoenix-dev`
still reached the expected runtime markers:

- `(psh)%`
- `help`
- `Available commands:`

So this Pi 4-specific change did not introduce a visible generic runtime
regression.

### Artifact Validation

Passed:

- `./scripts/assemble-rpi4b-bootfs.sh`
- `./scripts/assemble-rpi4b-bootfs-img.sh`
- `./scripts/assemble-rpi4b-sdimg.sh`
- `./scripts/export-rpi4b-sdimg.sh`
- `./scripts/verify-rpi4b-sdimg.sh`

## Refreshed Artifact

Exported host-visible image:

- `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`

Size:

- `69206016`

SHA-256:

- `254712ec591df30ec2368d783e4ad3c9ddf50f80613faad64c340bf8a1fa9ec3`

## Result

The next real-device retry should now use the refreshed image with corrected
ARM-visible Pi 4 GIC addresses in `plo`. If the board still remains black and
silent after this image, the next step should move even earlier, toward a
minimal earliest-entry diagnostic rather than another MMIO-address cleanup.
