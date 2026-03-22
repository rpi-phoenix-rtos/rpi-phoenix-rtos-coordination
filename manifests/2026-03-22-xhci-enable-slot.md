# STEP-0376

## Title

Implement the smallest xHCI `Enable Slot` step

## Date

2026-03-22

## Repositories

- `phoenix-rtos-devices`
- coordination repo

## Change Summary

The Pi 4 xHCI path now contains the first bounded post-roothub child-device
command path.

The new code in `usb/xhci/xhci.c` now adds:

- a small reusable internal command-execution helper derived from the earlier
  no-op path
- command-completion slot-ID extraction
- a bounded `Enable Slot` command helper that validates the returned slot ID
- a stored `slotId` field in the local xHCI state

The current init path now uses that helper after the earlier no-op command
check, so the first real child-device xHCI command is now exercised in the
controller bring-up path.

The step still intentionally stays bounded:

- it does not add `Address Device`
- it does not add endpoint-0 transfer support
- it does not add broad non-roothub transfer support
- it does not stage `/sbin/usb` or `/sbin/usbkbd` on the Pi 4 image yet

## Files

- `sources/phoenix-rtos-devices/usb/xhci/xhci.c`

## Validation

Validated in `phoenix-dev` with a fresh copied-buildroot Pi 4 A72 build:

```sh
./scripts/prepare-buildroot.sh --copy-components
cd ~/phoenix-buildroots/phoenix-rtos-project-copy
export PATH="$HOME/phoenix-toolchains/aarch64-phoenix/bin:$PATH"
export RPI4B_DTB_PATH=/tmp/rpi4b-dtb/bcm2711-rpi-4-b.dtb
export RPI4B_QEMU_MEMORY_SIZE=80000000
TARGET=aarch64a72-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image
```

Result:

- build completed successfully
- the staged Pi 4 image composition remained unchanged

Important validation limit:

- there is still no faithful no-hardware runtime lane for the Pi 4
  BCM2711 PCIe -> VL805 xHCI path
- so this step is compile-validated preparation, not yet runtime-proven

## Result

Phoenix now has the first real child-device xHCI command beyond the roothub:

- `Enable Slot`
- completion parsing
- slot-ID capture

The next blocker is now narrower and explicit:

- the controller still has no `Address Device` path
- there is still no endpoint-0 ring/context or real non-roothub control path

## Upstream Commit

- `phoenix-rtos-devices 32c6f09`

## Next Step

- scope the smallest xHCI `Address Device` step
