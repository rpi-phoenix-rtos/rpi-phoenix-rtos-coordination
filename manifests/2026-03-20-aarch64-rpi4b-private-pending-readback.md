# Manifest: Pi 4 Private Pending Readback

- Date: `2026-03-20`
- Step: `STEP-0204`
- Status: `completed`

## Goal

- determine whether the current Pi 4 timer probe is using the wrong GIC pending
  register view for a private timer interrupt

## Implementation

- added `interrupts_getPrivatePending()` in
  `sources/phoenix-rtos-kernel/hal/aarch64/interrupts_gicv2.c`
- kept the change diagnostic-only
- extended the first-arm timer probe in
  `sources/phoenix-rtos-kernel/hal/aarch64/gtimer_timer.c`
  to print one extra `gtimer: ppi pending ...` line

## Validation

### Generic `virt` guardrail lane

- Build:
  - `LIBPHOENIX_DEVEL_MODE=n TARGET=aarch64a53-generic-qemu ./phoenix-rtos-build/build.sh clean host core project image`
- Emulator:
  - `qemu-system-aarch64 -machine virt,secure=on,gic-version=2 -cpu cortex-a53 -smp 1 -m 1G -serial mon:stdio -serial null -display none -kernel _boot/aarch64a53-generic-qemu/plo.elf -device loader,file=_boot/aarch64a53-generic-qemu/loader.disk,addr=0x48000000,force-raw=on`
- Key evidence:
  - `gtimer: pending 1`
  - `gtimer: ppi pending 0`
  - `gic: timer dispatch`
  - `threads: timer irq`

### Pi 4 A72 patched lane

- Build:
  - `LIBPHOENIX_DEVEL_MODE=n RPI4B_DTB_PATH=$HOME/external/raspberrypi-firmware/boot/bcm2711-rpi-4-b.dtb RPI4B_QEMU_MEMORY_SIZE=80000000 TARGET=aarch64a72-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image`
- Emulator:
  - `$HOME/tools/qemu-10.2.2/bin/qemu-system-aarch64 -M raspi4b -cpu cortex-a72 -smp 4 -m 2G -nographic -monitor none -kernel _boot/aarch64a72-generic-rpi4b/plo.elf -device loader,file=_boot/aarch64a72-generic-rpi4b/rpi4b/loader.disk,addr=0x48000000,force-raw=on`
- Key evidence:
  - `gtimer: pending 0`
  - `gtimer: ppi pending 0`
  - `gtimer: post 2000 us ctl 0x5 ...`
  - no `gic: timer dispatch`
  - no `threads: timer irq`

## Result

- the private pending-state view does not explain the Pi 4 failure
- `PPISR` remains `0` even on the working generic lane, where:
  - `ISPENDR`-based pending becomes `1`
  - the timer dispatches successfully
- so the next step should not assume `PPISR` is the missing GIC signal source

## Next Step

- scope one bounded timer-group experiment, because the most visible remaining
  difference is now:
  - generic lane timer registration reads back `grp 0 en 1`
  - Pi 4 lane timer registration reads back `grp 1 en 1`
