# Manifest: Pi 4 Local Prescaler Experiment

- Date: `2026-03-20`
- Step: `STEP-0211`
- Status: `completed`

## Goal

- test whether adding Circle's Pi 4 local prescaler write changes the local
  pending or timer-dispatch behavior on the Pi 4 A72 QEMU lane

## Implementation

- added default `ARM_LOCAL_PRESCALER_VALUE` support in
  `sources/phoenix-rtos-kernel/hal/aarch64/generic/config.h`
- wrote the optional local prescaler value once during local controller setup in
  `sources/phoenix-rtos-kernel/hal/aarch64/interrupts_gicv2.c`
- added Pi 4-only `ARM_LOCAL_PRESCALER_VALUE 39768216u` in
  `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/board_config.h`

## Validation

### Generic `virt` guardrail lane

- Build:
  - `LIBPHOENIX_DEVEL_MODE=n TARGET=aarch64a53-generic-qemu ./phoenix-rtos-build/build.sh clean host core project image`
- Key evidence:
  - build succeeds with the common-code changes

### Pi 4 A72 patched lane

- Build:
  - `LIBPHOENIX_DEVEL_MODE=n RPI4B_DTB_PATH=$HOME/external/raspberrypi-firmware/boot/bcm2711-rpi-4-b.dtb RPI4B_QEMU_MEMORY_SIZE=80000000 TARGET=aarch64a72-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image`
- Emulator:
  - `$HOME/tools/qemu-10.2.2/bin/qemu-system-aarch64 -M raspi4b -cpu cortex-a72 -smp 4 -m 2G -nographic -monitor none -kernel _boot/aarch64a72-generic-rpi4b/plo.elf -device loader,file=_boot/aarch64a72-generic-rpi4b/rpi4b/loader.disk,addr=0x48000000,force-raw=on`
- Key evidence:
  - `gic: local prescaler 39768216`
  - `gic: local timer route 0x2`
  - `gic: timer handler set grp 1 en 1`
  - `gtimer: pending 0`
  - `gtimer: ppi pending 0`
  - `gtimer: local pending 0x0`
  - no `gic: timer dispatch`

## Result

- the prescaler write is accepted and traced, but it does not change the Pi 4
  QEMU behavior
- the Pi 4 QEMU lane still never shows local pending or GIC dispatch after the
  timer expires

## Next Step

- analyze the QEMU `raspi4b` timer wiring directly, because the local
  controller experiments now point toward an emulator-specific path split
