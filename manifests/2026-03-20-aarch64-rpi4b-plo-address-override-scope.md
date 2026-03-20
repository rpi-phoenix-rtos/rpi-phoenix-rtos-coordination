# Manifest: Pi 4 `plo` Address Override Scope

- Date: `2026-03-20`
- Step: `STEP-0110`
- Status: `completed`

## Goal

- define the smallest next step that removes the current Pi 4 `plo` board-address blocker without breaking the existing generic QEMU lane

## Source Findings

From the current generic loader HAL:

- `sources/plo/hal/aarch64/generic/config.h`
  - hardcodes:
    - `GICD_BASE_ADDRESS ((void *)0x08000000)`
    - `GICC_BASE_ADDRESS ((void *)0x08010000)`
    - `UART0_BASE_ADDRESS ((void *)0x09000000)`
- `sources/plo/hal/aarch64/generic/console.c`
  - uses `UART0_BASE_ADDRESS` directly
- `sources/plo/hal/aarch64/generic/interrupts.c`
  - uses `GICD_BASE_ADDRESS` and `GICC_BASE_ADDRESS` directly

From the current build wiring:

- `sources/phoenix-rtos-build/Makefile.common`
  - prepends `-I$(PROJECT_PATH)` to `CFLAGS`
  - this means `plo` can already consume project-local `board_config.h` overrides without adding a new include path

From the current Pi 4 project scaffold:

- `sources/phoenix-rtos-project/_projects/aarch64a53-generic-rpi4b/board_config.h`
  - already defines Pi 4 runtime PL011 values for `pl011-tty`
  - does not yet define `plo`-side UART or GIC override macros

From Raspberry Pi 4 DT reference material:

- the Pi 4 DT GIC node is `compatible = "arm,gic-400"`
- the GIC distributor and CPU-interface base addresses are `0x40041000` and `0x40042000`
- the primary PL011 UART resolves to `0xfe201000` after the SoC bus-range translation

From the current VM QEMU inventory:

- `qemu-system-aarch64 -machine help` in `phoenix-dev` lists `raspi0`, `raspi1ap`, `raspi2b`, `raspi3ap`, and `raspi3b`
- it does not list `raspi4b`

## Selected Next Step

- keep the change narrow and loader-focused
- teach `sources/plo/hal/aarch64/generic/config.h` to use optional `board_config.h` macros for:
  - GIC distributor base
  - GIC CPU-interface base
  - UART0 base
- preserve the current QEMU `virt` values as defaults when no board override is supplied
- extend the Pi 4 project-local `board_config.h` with the matching `plo` override macros

## Planned Validation For The Next Step

- build:
  - `LIBPHOENIX_DEVEL_MODE=n TARGET=aarch64a53-generic-qemu ./phoenix-rtos-build/build.sh clean host core project image`
  - `LIBPHOENIX_DEVEL_MODE=n TARGET=aarch64a53-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image`
- emulator:
  - rerun the known-good generic QEMU smoke lane to prove the default QEMU `virt` path still boots
- artifact inspection:
  - confirm the Pi 4 loader image now compiles with board-local MMIO address overrides instead of the generic QEMU hardcoded set
