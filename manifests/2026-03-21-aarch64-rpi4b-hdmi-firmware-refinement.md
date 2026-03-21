# 2026-03-21: implement the Pi 4 firmware-stage HDMI refinement

## Scope

- Step: `STEP-0287`
- Goal: apply the smallest firmware-stage HDMI refinement selected in
  `STEP-0286`

## Repositories Touched

- `phoenix-rtos-project`
- coordination repo

## Change

Updated the Pi 4 project firmware staging config:

- `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/config.txt`

Added:

- `hdmi_force_hotplug=1`
- `disable_overscan=1`

## Rationale

Based on current official Raspberry Pi legacy `config.txt` documentation:

- `hdmi_force_hotplug=1` forces HDMI output mode even if hotplug detection does
  not assert
- `disable_overscan=1` disables the firmware default overscan margins

For the current no-UART Pi 4 lab, that is the narrowest useful firmware-stage
refinement for the early `plo` HDMI marker:

- make HDMI output more likely to appear
- make the upper-left marker less likely to be cropped

## Validation

Rebuilt only the Pi 4 project/image lane in `phoenix-dev`:

```sh
./scripts/prepare-buildroot.sh --copy-components /home/witoldbolt.guest/phoenix-buildroots/phoenix-rtos-project-copy
cd /home/witoldbolt.guest/phoenix-buildroots/phoenix-rtos-project-copy
export PATH=/home/witoldbolt.guest/phoenix-toolchains/aarch64-phoenix/bin:$PATH
export RPI4B_DTB_PATH=/home/witoldbolt.guest/external/raspberrypi-firmware/boot/bcm2711-rpi-4-b.dtb
export RPI4B_QEMU_MEMORY_SIZE=80000000
export LIBPHOENIX_DEVEL_MODE=n
TARGET=aarch64a72-generic-rpi4b ./phoenix-rtos-build/build.sh project image
```

Verified the staged artifact:

```text
_boot/aarch64a72-generic-rpi4b/rpi4b/config.txt
```

Observed staged lines:

```text
hdmi_force_hotplug=1
disable_overscan=1
```

## Result

- the first real Pi 4 HDMI trial now uses a slightly hardened firmware config
  without widening into explicit fixed display modes or broader safe-mode
  bundles
- the next practical step should refresh the host-visible Pi 4 SD-card artifact
  so the operator flashes the refined image rather than an older export
