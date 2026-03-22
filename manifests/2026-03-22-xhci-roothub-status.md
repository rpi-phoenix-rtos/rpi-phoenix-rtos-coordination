# STEP-0374

## Title

Implement the smallest xHCI roothub status-delivery step

## Date

2026-03-22

## Repositories

- `phoenix-rtos-devices`
- coordination repo

## Change Summary

The Pi 4 xHCI path now contains a temporary root-hub status-delivery bridge for
the current no-IRQ path.

The new code:

- adds a small xHCI background thread after successful controller init
- polls `xhci_getHubStatus()` periodically
- checks for a pending root-hub interrupt transfer
- completes that pending transfer when roothub change bits appear

The step is intentionally still bounded:

- it does not add a real xHCI interrupt path
- it does not add non-roothub transfers
- it does not add child-device enumeration
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

The xHCI path is now closer to a useful staged `/sbin/usb` daemon:

- the controller can survive init
- roothub requests exist
- roothub status changes now have a bounded temporary delivery path even before
  a real interrupt path exists

The next blocker is now explicit:

- child-device enumeration still has no non-roothub xHCI transfer path
- keyboard support still depends on the first real xHCI device-enumeration
  seam after the roothub

## Upstream Commit

- `phoenix-rtos-devices 10f29da`

## Next Step

- scope the smallest xHCI non-roothub device-enumeration step
