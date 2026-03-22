# 2026-03-22: Pi 4 SD-image refresh for first HDMI plus USB-keyboard trial

## Scope

Close `STEP-0394` by rebuilding, regenerating, and exporting the Pi 4 SD-card
image after live `/sbin/usb` staging.

## Rebuild And Image Commands

Pi 4 rebuild in `phoenix-dev`:

```sh
export PATH="$HOME/phoenix-toolchains/aarch64-phoenix/bin:$PATH"
cd ~/phoenix-buildroots/phoenix-rtos-project-copy
/Users/witoldbolt/phoenix-rpi/scripts/prepare-buildroot.sh --copy-components
export RPI4B_DTB_PATH=/tmp/rpi4b-dtb/bcm2711-rpi-4-b.dtb
export RPI4B_QEMU_MEMORY_SIZE=80000000
TARGET=aarch64a72-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image
```

Image regeneration:

```sh
/Users/witoldbolt/phoenix-rpi/scripts/assemble-rpi4b-bootfs.sh
/Users/witoldbolt/phoenix-rpi/scripts/assemble-rpi4b-bootfs-img.sh
/Users/witoldbolt/phoenix-rpi/scripts/assemble-rpi4b-sdimg.sh
/Users/witoldbolt/phoenix-rpi/scripts/export-rpi4b-sdimg.sh
```

## Exported Artifact

- host path:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- size:
  `69206016`
- SHA-256:
  `475d8d21cdc00d2c2fc79819fe02bdcc946b5ee75329b503198dda7ac16877c3`

## Embedded Boot Content

The exported image contains:

- Raspberry Pi firmware boot files from the staged Pi 4 boot tree
- `kernel8.img`
- `loader.disk`
- the current Phoenix Pi 4 program chain:
  - `pcie`
  - `usb`
  - `psh`

## Outcome

- the first refreshed Pi 4 SD-card image for HDMI plus USB-keyboard testing is
  now exported to the coordination repository artifact area
- the next meaningful validation step is manual execution on the real Raspberry
  Pi 4 board
