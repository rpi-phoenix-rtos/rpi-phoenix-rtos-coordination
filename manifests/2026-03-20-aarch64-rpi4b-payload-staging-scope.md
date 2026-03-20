# Manifest: Pi 4 Firmware Payload Staging Scope

- Date: `2026-03-20`
- Step: `STEP-0106`
- Status: `completed`

## Goal

- define the smallest Pi 4 boot-tree follow-up after the multi-EL loader step so a firmware-booted `kernel8.img` can access the existing Phoenix payload path without first adding SD, FAT, or network drivers

## Source Findings

From the generic AArch64 target:

- `sources/phoenix-rtos-project/_targets/aarch64a53/generic/build.project`
  - exports `BOOT_DEVICE="ram0"`
  - builds `loader.disk`
- `sources/phoenix-rtos-project/_targets/aarch64a53/generic/preinit.plo.yaml`
  - maps DDR
  - initializes `phfs ram0 4.0 raw`
  - loads `user.plo` from the `loader` image inside that raw medium
- `sources/plo/hal/aarch64/generic/config.h`
  - fixes `RAM_ADDR` at `0x48000000`
  - fixes `RAM_BANK_SIZE` at `0x08000000`

From the current Pi 4 project-local files:

- `sources/phoenix-rtos-project/_projects/aarch64a53-generic-rpi4b/build.project`
  - stages `config.txt`
  - stages `kernel8.img`
  - optionally stages `bcm2711-rpi-4-b.dtb`
  - does not currently stage `loader.disk`
- `sources/phoenix-rtos-project/_projects/aarch64a53-generic-rpi4b/user.plo.yaml`
  - still expects `{{ env.BOOT_DEVICE }}` content to exist

## External Reference

Official Raspberry Pi `config.txt` documentation confirms:

- `ramfsaddr` sets the memory address where a `ramfsfile` is loaded
- `initramfs` can specify both filename and load address in one directive
- the load address may be an explicit address such as `0x48000000`

Reference:

- <https://www.raspberrypi.com/documentation/computers/config_txt.html>

## Selected Next Step

- keep the existing generic `ram0` payload contract
- stage `loader.disk` into the Pi 4 firmware-facing boot tree
- update the Pi 4 project-local `config.txt` to make Raspberry Pi firmware load that file to `0x48000000`
- keep the change project-local to `phoenix-rtos-project`

## Why This Step Wins

- it reuses the current generic `plo` boot flow instead of inventing a second board-specific payload path
- it avoids premature SD, FAT, or network driver work
- it stays compatible with the current generic `ram-storage` device already built into generic AArch64 `plo`

## Planned Validation For The Next Step

- build:
  - `TARGET=aarch64a53-generic-rpi4b ./phoenix-rtos-build/build.sh host core project image`
- artifact inspection:
  - `_boot/aarch64a53-generic-rpi4b/rpi4b/config.txt`
  - `_boot/aarch64a53-generic-rpi4b/rpi4b/kernel8.img`
  - `_boot/aarch64a53-generic-rpi4b/rpi4b/loader.disk`
- consistency checks:
  - staged `loader.disk` size fits within `RAM_BANK_SIZE`
  - configured load address matches `RAM_ADDR`
