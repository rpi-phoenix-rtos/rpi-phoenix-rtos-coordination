# Manifest: Pi 4 `plo` MMIO Address Overrides

- Date: `2026-03-20`
- Step: `STEP-0111`
- Status: `completed`
- Upstream repositories:
  - `sources/plo`
  - `sources/phoenix-rtos-project`
- Upstream commits:
  - `sources/plo`: `1ea8e70` (`aarch64/generic: allow board-config MMIO overrides`)
  - `sources/phoenix-rtos-project`: `96cf17c` (`project: add rpi4b plo MMIO overrides`)

## Goal

- stop the Pi 4 `kernel8.img` loader path from assuming QEMU `virt` UART and GIC base addresses while preserving the existing generic QEMU fallback

## Changes

In `sources/plo/hal/aarch64/generic/config.h`:

- include `board_config.h`
- add optional board-config override macros:
  - `PLO_GICD_BASE_ADDRESS`
  - `PLO_GICC_BASE_ADDRESS`
  - `PLO_UART0_BASE_ADDRESS`
- preserve the existing QEMU `virt` values as defaults when the project does not define overrides

In `sources/phoenix-rtos-project/_projects/aarch64a53-generic-rpi4b/board_config.h`:

- define Pi 4 loader MMIO overrides:
  - `PLO_GICD_BASE_ADDRESS  0x40041000u`
  - `PLO_GICC_BASE_ADDRESS  0x40042000u`
  - `PLO_UART0_BASE_ADDRESS 0xfe201000u`

## Validation

Validation ran in `phoenix-dev` from the copied buildroot:

- prepare buildroot:
  - `./scripts/prepare-buildroot.sh --copy-components /home/witoldbolt.guest/phoenix-buildroots/phoenix-step0111`

- generic QEMU build:
  - `LIBPHOENIX_DEVEL_MODE=n TARGET=aarch64a53-generic-qemu ./phoenix-rtos-build/build.sh clean host core project image`
  - result: build passed

- generic QEMU smoke:
  - `timeout 12s qemu-system-aarch64 -machine virt,secure=on,gic-version=2 -cpu cortex-a53 -smp 1 -m 1G -serial mon:stdio -serial null -display none -kernel _boot/aarch64a53-generic-qemu/plo.elf -device loader,file=_boot/aarch64a53-generic-qemu/loader.disk,addr=0x48000000,force-raw=on`
  - visible output still reached:
    - loader banner
    - kernel banner
    - `pl011-tty: started`

- Pi 4 project build:
  - `LIBPHOENIX_DEVEL_MODE=n TARGET=aarch64a53-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image`
  - result: build passed

- macro-resolution probe:
  - the same generic loader `config.h` was preprocessed twice inside `phoenix-step0111`
  - `generic-qemu` resolved to:
    - `PLO_GICD_BASE_ADDRESS 0x08000000u`
    - `PLO_GICC_BASE_ADDRESS 0x08010000u`
    - `PLO_UART0_BASE_ADDRESS 0x09000000u`
  - `generic-rpi4b` resolved to:
    - `PLO_GICD_BASE_ADDRESS 0x40041000u`
    - `PLO_GICC_BASE_ADDRESS 0x40042000u`
    - `PLO_UART0_BASE_ADDRESS 0xfe201000u`

- firmware-load-address check:
  - `sources/plo/ld/aarch64a53-generic.ldt` sets `ADDR_PLO 0x40080000`
  - `sources/phoenix-rtos-project/_projects/aarch64a53-generic-rpi4b/config.txt` sets `kernel_address=0x40080000`
  - result: the current Pi 4 firmware handoff does not have a raw loader-load-address mismatch

## Outcome

- the generic AArch64 loader now keeps the existing `virt` defaults for QEMU but can consume Pi 4-specific MMIO bases through project-local board config
- the Pi 4 boot candidate is closer to a first real board boot because its loader console and GIC initialization path now target BCM2711 MMIO addresses instead of QEMU `virt`
- the next bounded step should focus on producing a reproducible deployment artifact or first hardware-smoke workflow rather than reopening generic loader address assumptions
