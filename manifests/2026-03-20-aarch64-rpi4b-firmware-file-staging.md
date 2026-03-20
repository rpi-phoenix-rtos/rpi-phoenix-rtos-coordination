# Manifest: Pi 4 Firmware-File Staging

- Date: `2026-03-20`
- Step: `STEP-0113`
- Status: `completed`
- Upstream repositories:
  - `sources/phoenix-rtos-project`
- Upstream commits:
  - `sources/phoenix-rtos-project`: `df51133` (`project: stage rpi4b firmware files`)

## Goal

- let the Pi 4 project consume an operator-supplied Raspberry Pi firmware-file set and stage it into the firmware-facing boot tree without breaking default no-firmware builds

## Changes

In `sources/phoenix-rtos-project/_projects/aarch64a53-generic-rpi4b/build.project`:

- add optional Pi 4 firmware-directory discovery:
  - caller-supplied `RPI4B_FIRMWARE_DIR`
  - fallback project-local directory `_projects/aarch64a53-generic-rpi4b/firmware`
- add `rpi4b_stageFirmware()` helper
- require these files when a firmware directory is supplied:
  - `start4.elf`
  - `fixup4.dat`
- stage these files when present:
  - `start4db.elf`
  - `fixup4db.dat`
  - `start4cd.elf`
  - `fixup4cd.dat`
- keep default no-firmware builds green by making the staging path optional
- fix the initial environment-variable handling bug so caller-supplied `RPI4B_FIRMWARE_DIR` survives script initialization

## Validation

Validation ran in `phoenix-dev` from the copied buildroot:

- prepare buildroot:
  - `./scripts/prepare-buildroot.sh --copy-components /home/witoldbolt.guest/phoenix-buildroots/phoenix-step0113-firmware`

- default Pi 4 build:
  - `PATH="$HOME/phoenix-toolchains/aarch64-phoenix/bin:$PATH" LIBPHOENIX_DEVEL_MODE=n TARGET=aarch64a53-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image`
  - result: build passed
  - artifact checks passed:
    - `_boot/aarch64a53-generic-rpi4b/rpi4b/kernel8.img` exists
    - `_boot/aarch64a53-generic-rpi4b/rpi4b/loader.disk` exists
    - `_boot/aarch64a53-generic-rpi4b/rpi4b/start4.elf` does not exist
    - `_boot/aarch64a53-generic-rpi4b/rpi4b/fixup4.dat` does not exist

- supplied-firmware Pi 4 build:
  - create synthetic firmware input directory:
    - `start4.elf`
    - `fixup4.dat`
    - `start4db.elf`
  - `PATH="$HOME/phoenix-toolchains/aarch64-phoenix/bin:$PATH" RPI4B_FIRMWARE_DIR="$HOME/tmp/rpi4b-fw-step0113" LIBPHOENIX_DEVEL_MODE=n TARGET=aarch64a53-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image`
  - result: build passed
  - artifact checks passed:
    - `_boot/aarch64a53-generic-rpi4b/rpi4b/start4.elf` exists
    - `_boot/aarch64a53-generic-rpi4b/rpi4b/fixup4.dat` exists
    - `_boot/aarch64a53-generic-rpi4b/rpi4b/start4db.elf` exists

## Outcome

- the Pi 4 staged boot tree can now include operator-supplied Raspberry Pi firmware files as part of the existing project-local build flow
- the no-hardware build lane remains green when firmware files are omitted
- the staged tree is now closer to a self-contained first-partition boot bundle, although a realistic Pi 4 boot still also needs a real DTB and a verified firmware-file baseline rather than synthetic placeholders
