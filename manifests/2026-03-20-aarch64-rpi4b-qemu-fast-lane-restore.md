# Manifest: Pi 4 QEMU Fast Lane Restore

- Date: `2026-03-20`
- Step: `STEP-0213`
- Status: `completed`

## Goal

- remove the now-proven-irrelevant local-controller experiment hooks from the
  Pi 4 QEMU fast lane and restore the last relevant GTIMER-to-GIC baseline

## Implementation

- removed the temporary local controller macros and route/prescaler handling
  from:
  - `sources/phoenix-rtos-kernel/hal/aarch64/generic/config.h`
  - `sources/phoenix-rtos-kernel/hal/aarch64/interrupts_gicv2.h`
  - `sources/phoenix-rtos-kernel/hal/aarch64/interrupts_gicv2.c`
  - `sources/phoenix-rtos-kernel/hal/aarch64/gtimer_timer.c`
- removed the Pi 4-only local controller board config hooks from
  `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/board_config.h`

## Validation

### Generic `virt` guardrail lane

- Build:
  - `LIBPHOENIX_DEVEL_MODE=n TARGET=aarch64a53-generic-qemu ./phoenix-rtos-build/build.sh clean host core project image`
- Key evidence:
  - build succeeds after removing the detour code

### Pi 4 A72 patched lane

- Build:
  - `LIBPHOENIX_DEVEL_MODE=n RPI4B_DTB_PATH=$HOME/external/raspberrypi-firmware/boot/bcm2711-rpi-4-b.dtb RPI4B_QEMU_MEMORY_SIZE=80000000 TARGET=aarch64a72-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image`
- Emulator:
  - `$HOME/tools/qemu-10.2.2/bin/qemu-system-aarch64 -M raspi4b -cpu cortex-a72 -smp 4 -m 2G -nographic -monitor none -kernel _boot/aarch64a72-generic-rpi4b/plo.elf -device loader,file=_boot/aarch64a72-generic-rpi4b/rpi4b/loader.disk,addr=0x48000000,force-raw=on`
- Key evidence:
  - `gic: timer handler set grp 1 en 1`
  - `gtimer: pending 0`
  - `gtimer: ppi pending 0`
  - `gtimer: post 2000 us ctl 0x5 ...`
  - no `gic: timer dispatch`

## Result

- the Pi 4 QEMU lane is back to the pre-local-controller evidence set
- the next bounded move can now target only the direct GTIMER-to-GIC path
  proven by QEMU `bcm2838.c`

## Next Step

- scope a one-variable direct GIC CPU-interface pending-view probe on the Pi 4
  QEMU lane
