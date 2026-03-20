# Manifest: Pi 4 Local Interrupt Routing Experiment

- Date: `2026-03-20`
- Step: `STEP-0209`
- Status: `completed`

## Goal

- test whether the Pi 4 timer path is missing only the BCM2711 local interrupt
  controller route enable for the non-secure physical timer

## Implementation

- added Pi 4-only `ARM_LOCAL_BASE 0xff800000u` in
  `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/board_config.h`
- added generic AArch64 local interrupt controller constants in
  `sources/phoenix-rtos-kernel/hal/aarch64/generic/config.h`
- mapped the optional local interrupt controller window and enabled
  `ARM_LOCAL_TIMER_INT_CONTROL0` bit `1` when the timer IRQ handler is
  registered in
  `sources/phoenix-rtos-kernel/hal/aarch64/interrupts_gicv2.c`
- exposed `interrupts_getLocalPending()` in
  `sources/phoenix-rtos-kernel/hal/aarch64/interrupts_gicv2.h`
- extended the existing timer probe in
  `sources/phoenix-rtos-kernel/hal/aarch64/gtimer_timer.c`
  to print one bounded `gtimer: local pending ...` line

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
  - `gic: local timer route 0x2`
  - `gic: timer handler set grp 1 en 1`
  - `gtimer: pending 0`
  - `gtimer: ppi pending 0`
  - `gtimer: local pending 0x0`
  - no `gic: timer dispatch`

## Result

- the route-enable write is accepted and read back as `0x2`
- enabling `ARM_LOCAL_TIMER_INT_CONTROL0` alone does not make the local
  pending bit appear and does not restore timer dispatch
- the next bounded move should stay in the same local interrupt controller
  block and test the Pi 4 local prescaler value seen in Circle

## Next Step

- scope a one-variable Pi 4 local prescaler experiment using Circle's
  `ARM_LOCAL_PRESCALER = 39768216U` write in `external/circle/lib/sysinit.cpp`
